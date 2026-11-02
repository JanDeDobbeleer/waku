#include "features/apps/qml/apps_service.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QQmlEngine>
#include <QThreadPool>
#include <algorithm>

#include "features/apps/rank.h"
#include "kernel/qt/app_data.h"
#include "kernel/qt/linger.h"
#include "kernel/qt/live_desktop.h"

namespace waku::apps {

AppCatalog::AppCatalog(QObject* parent) : QObject(parent) {
  watcher_ = new QFileSystemWatcher(this);
  connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
    fresh_ = false;
    emit foldersChanged();
  });
}

void AppCatalog::store(std::vector<AppRecord> catalog, int scanMs) {
  catalog_ = std::move(catalog);
  scanMs_ = scanMs;
  fresh_ = true;
}

void AppCatalog::watch(const QStringList& directories) {
  const QStringList current = watcher_->directories();
  QStringList gone;
  for (const QString& directory : current) {
    if (!directories.contains(directory)) gone.append(directory);
  }
  QStringList added;
  for (const QString& directory : directories) {
    if (!current.contains(directory)) added.append(directory);
  }
  if (!gone.isEmpty()) watcher_->removePaths(gone);
  if (!added.isEmpty()) watcher_->addPaths(added);
}

QStringList AppCatalog::watched() const { return watcher_->directories(); }

std::shared_ptr<AppCatalog> acquireAppCatalog() {
  static kernel::LingeringHub<AppCatalog> hub;
  return hub.acquire([] { return std::make_shared<AppCatalog>(); });
}

namespace {

std::function<std::unique_ptr<AppsService>()>& testFactory() {
  static std::function<std::unique_ptr<AppsService>()> factory;
  return factory;
}

AppsService::Deps realDeps() {
  AppsService::Deps deps;
  if (!waku::kernel::liveDesktop(QStringLiteral("apps"))) return deps;
  deps.folders = startMenuFolders();
  deps.resolver = makeWin32ShortcutResolver();
  deps.appsFolder = makeWin32AppsFolderEnumerator();
  deps.launcher = makeWin32Launcher();
  deps.iconLoader = realIconLoader();
  deps.frecencyPath = waku::kernel::appDataDir() + QStringLiteral("/apps_frecency.json");
  deps.watch = true;
  deps.catalog = acquireAppCatalog();
  return deps;
}

}  // namespace

AppsService::AppsService(QObject* parent) : AppsService(realDeps(), parent) {}

AppsService::AppsService(Deps deps, QObject* parent)
    : QObject(parent), deps_(std::move(deps)), model_(this), frecency_(deps_.frecencyPath) {
  frecency_.load();
  state_->owner = this;
  debounce_.setSingleShot(true);
  debounce_.setInterval(deps_.debounceMs);
  connect(&debounce_, &QTimer::timeout, this, &AppsService::refresh);
  if (deps_.catalog) {
    connect(deps_.catalog.get(), &AppCatalog::foldersChanged, &debounce_,
            qOverload<>(&QTimer::start));
    if (deps_.catalog->fresh()) {
      // The catalogue of the engine before a reload; nothing changed on disk since.
      scanning_ = true;
      ScanResult cached;
      cached.catalog = deps_.catalog->catalog();
      cached.complete = true;
      cached.ms = deps_.catalog->scanMs();
      applyResult(std::move(cached));
      return;
    }
  } else if (deps_.watch) {
    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, &debounce_,
            qOverload<>(&QTimer::start));
  }
  refresh();
}

AppsService::~AppsService() {
  // A scan still running posts to nobody: its sink checks `owner` under this lock.
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->owner = nullptr;
}

AppsService* AppsService::create(QQmlEngine* engine, QJSEngine*) {
  // The engine owns the returned singleton and deletes its image providers.
  AppsService* service = nullptr;
  if (testFactory()) service = testFactory()().release();
  if (!service) service = new AppsService(nullptr);
  if (engine) {
    engine->addImageProvider(QStringLiteral("appicon"),
                             new AppIconProvider(service->iconIndex(), service->iconLoader()));
  }
  return service;
}

void AppsService::setFactoryForTests(std::function<std::unique_ptr<AppsService>()> factory) {
  testFactory() = std::move(factory);
}

DesktopEntry* AppsService::find(const QString& id) const {
  return entries_.value(id.toLower(), nullptr);
}

const AppRecord* AppsService::recordFor(const QString& id) const {
  const DesktopEntry* entry = find(id);
  return entry ? &entry->record() : nullptr;
}

DesktopEntry* AppsService::byId(const QString& id) const { return find(id); }

DesktopEntry* AppsService::heuristicLookup(const QString& nameIn) const {
  QString name = nameIn.trimmed().toLower();
  if (name.endsWith(QLatin1String(".exe"))) name.chop(4);
  if (name.isEmpty()) return nullptr;
  if (DesktopEntry* entry = find(name)) return entry;
  const auto& entries = model_.entries();
  const auto first = [&](const std::function<bool(const AppRecord&)>& match) -> DesktopEntry* {
    for (DesktopEntry* entry : entries) {
      if (match(entry->record())) return entry;
    }
    return nullptr;
  };
  if (DesktopEntry* entry =
          first([&](const AppRecord& r) { return r.name.compare(name, Qt::CaseInsensitive) == 0; }))
    return entry;
  if (DesktopEntry* entry = first([&](const AppRecord& r) {
        return startupClassFor(r).compare(name, Qt::CaseInsensitive) == 0;
      }))
    return entry;
  if (DesktopEntry* entry = first([&](const AppRecord& r) {
        return r.id.section(QLatin1Char('/'), -1).compare(name, Qt::CaseInsensitive) == 0;
      }))
    return entry;
  // A Store app's AUMID names its package ("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App").
  if (name.size() >= 3) {
    if (DesktopEntry* entry = first([&](const AppRecord& r) {
          return r.kind == AppKind::Store && r.aumid.contains(name, Qt::CaseInsensitive);
        }))
      return entry;
  }
  return nullptr;
}

QVariantList AppsService::search(const QString& query, int limit) const {
  QVariantList result;
  try {
    const auto& entries = model_.entries();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::vector<RankCandidate> candidates;
    candidates.reserve(static_cast<size_t>(entries.size()));
    for (const DesktopEntry* entry : entries) {
      const AppRecord& record = entry->record();
      RankCandidate candidate;
      candidate.name = record.name;
      candidate.extras = record.keywords;
      if (!record.genericName.isEmpty()) candidate.extras.append(record.genericName);
      candidate.frecency = frecency_.score(record.id, now);
      candidates.push_back(std::move(candidate));
    }
    for (const int index : rankApps(query, candidates, limit))
      result.append(QVariant::fromValue(entries.at(index)));
  } catch (const std::exception& error) {
    qWarning() << "apps: search failed:" << error.what();
  }
  return result;
}

bool AppsService::launchMode(const QString& id, LaunchMode mode) {
  DesktopEntry* entry = find(id);
  if (!entry || !deps_.launcher) return false;
  try {
    const AppRecord record = entry->record();
    if (!deps_.launcher(record, mode)) return false;
    if (mode != LaunchMode::OpenLocation) {
      frecency_.record(record.id, QDateTime::currentMSecsSinceEpoch());
      frecency_.save();
    }
    return true;
  } catch (const std::exception& error) {
    qWarning() << "apps: launching" << id << "failed:" << error.what();
    return false;
  }
}

bool AppsService::launch(const QString& id) { return launchMode(id, LaunchMode::Normal); }
bool AppsService::launchAsAdmin(const QString& id) { return launchMode(id, LaunchMode::Admin); }
bool AppsService::openLocation(const QString& id) {
  return launchMode(id, LaunchMode::OpenLocation);
}

void AppsService::refresh() {
  if (scanning_) {
    rescan_ = true;
    return;
  }
  startScan();
}

void AppsService::startScan() {
  scanning_ = true;
  if (!loading_) {
    loading_ = true;
    emit loadingChanged();
  }
  // The shortcuts alone only while the list is empty: on a rescan they would drop every Store app
  // from the list until the complete result puts them back (two resets of every launcher list).
  const bool interim = model_.count() == 0;
  if (!deps_.async) {
    runScan(deps_, interim, [this](ScanResult result) { applyResult(std::move(result)); });
    return;
  }
  auto state = state_;
  QThreadPool::globalInstance()->start([deps = deps_, interim, state]() {
    try {
      runScan(deps, interim, [&state](ScanResult result) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->owner) return;
        auto shared = std::make_shared<ScanResult>(std::move(result));
        QMetaObject::invokeMethod(
            state->owner,
            [owner = state->owner, shared]() { owner->applyResult(std::move(*shared)); },
            Qt::QueuedConnection);
      });
    } catch (const std::exception& error) {
      qWarning() << "apps: scan failed:" << error.what();
    }
  });
}

void AppsService::runScan(const Deps& deps, bool interim,
                          const std::function<void(ScanResult)>& sink) {
  QElapsedTimer timer;
  timer.start();
  std::vector<AppRecord> shortcuts = scanShortcuts(deps.folders, deps.resolver);
  if (deps.appsFolder && interim) {
    // Shortcuts first; the Store apps take longer.
    ScanResult first;
    first.catalog = buildCatalog(shortcuts, {});
    first.ms = static_cast<int>(timer.elapsed());
    sink(std::move(first));
  }
  std::vector<AppRecord> store;
  if (deps.appsFolder) store = deps.appsFolder();
  ScanResult done;
  done.catalog = buildCatalog(std::move(shortcuts), std::move(store));
  done.complete = true;
  done.ms = static_cast<int>(timer.elapsed());
  sink(std::move(done));
}

void AppsService::applyResult(ScanResult result) {
  if (result.complete && deps_.catalog) deps_.catalog->store(result.catalog, result.ms);
  // Reuse the object of every id that is still there, so QML references survive a rescan.
  QHash<QString, DesktopEntry*> next;
  QList<DesktopEntry*> list;
  QHash<QString, QString> icons;
  list.reserve(static_cast<qsizetype>(result.catalog.size()));
  for (AppRecord& record : result.catalog) {
    const QString key = record.id.toLower();
    icons.insert(record.id, record.iconSource);
    DesktopEntry* entry = entries_.take(key);
    if (entry) {
      entry->setRecord(std::move(record));
    } else {
      entry = new DesktopEntry(
          std::move(record),
          [this](const QString& id, LaunchMode mode) { return launchMode(id, mode); }, this);
    }
    next.insert(key, entry);
    list.append(entry);
  }
  const int before = model_.count();
  QList<DesktopEntry*> gone = entries_.values();
  entries_ = std::move(next);
  iconIndex_->replace(std::move(icons));
  model_.setEntries(std::move(list));
  for (DesktopEntry* entry : gone) entry->deleteLater();
  if (model_.count() != before) emit countChanged();

  if (!result.complete) return;
  scanning_ = false;
  scanMs_ = result.ms;
  if (!ready_) {
    ready_ = true;
    emit readyChanged();
  }
  watchFolders();
  if (rescan_) {
    rescan_ = false;
    startScan();
    return;
  }
  if (loading_) {
    loading_ = false;
    emit loadingChanged();
  }
  emit scanFinished();
}

void AppsService::watchFolders() {
  if (deps_.catalog) {
    if (deps_.watch) deps_.catalog->watch(watchedDirectories(deps_.folders));
    return;
  }
  if (!watcher_) return;
  const QStringList old = watcher_->directories();
  if (!old.isEmpty()) watcher_->removePaths(old);
  const QStringList directories = watchedDirectories(deps_.folders);
  if (!directories.isEmpty()) watcher_->addPaths(directories);
}

}  // namespace waku::apps

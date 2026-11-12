#include "features/wallpapers/qml/wallpapers_service.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QScreen>
#include <QThreadPool>
#include <QVariantMap>
#include <QWindow>
#include <exception>
#include <utility>

#include "features/wallpapers/wallpaper_files.h"
#include "features/wallpapers/win32/window_focus.h"
#include "features/workspaces/virtual_desktops.h"
#include "kernel/qt/app_data.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/window_focus.h"

namespace waku::wallpapers {
namespace {

std::function<std::unique_ptr<Wallpapers>()>& testFactory() {
  static std::function<std::unique_ptr<Wallpapers>()> factory;
  return factory;
}

template <typename F>
void guarded(const char* what, F&& body) {
  try {
    body();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Wallpapers:" << what << "failed:" << error.what();
  } catch (...) {
    qWarning().noquote() << "Wallpapers:" << what << "failed";
  }
}

// The PNG thumbnails of the old image://wallpaper provider; the gallery uses image://thumb now.
// Nothing pruned that folder, so it is removed once, off the GUI thread.
void removeRetiredThumbnailFolder() {
  if (!kernel::liveDesktop(QStringLiteral("wallpapers"))) return;
  const QString folder =
      QDir(kernel::appDataDir()).filePath(QStringLiteral("wallpaper_thumbnails"));
  if (!QFileInfo::exists(folder)) return;
  QThreadPool::globalInstance()->start([folder] { QDir(folder).removeRecursively(); });
}

bool hasFolderPart(const QString& target) {
  return target.contains(QLatin1Char('/')) || target.contains(QLatin1Char('\\')) ||
         target.contains(QLatin1Char(':'));
}

QString nativePath(const QString& path) {
  return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

ResolvedWallpaper validateImageFile(const QString& path, const QStringList& types) {
  const QFileInfo info(path);
  if (!info.exists()) return {{}, QStringLiteral("no such file: %1").arg(nativePath(path))};
  if (!info.isFile()) return {{}, QStringLiteral("not a file: %1").arg(nativePath(path))};
  if (!types.contains(info.suffix().toLower())) {
    return {{},
            QStringLiteral("not a supported image type (%1): %2")
                .arg(types.join(QStringLiteral(", ")), nativePath(path))};
  }
  QImageReader reader(info.absoluteFilePath());
  if (!reader.canRead()) {
    return {{}, QStringLiteral("cannot read the image: %1").arg(nativePath(path))};
  }
  return {nativePath(path), {}};
}

QString okAnswer(const QString& path) { return QStringLiteral("ok: ") + path; }
QString errorAnswer(const QString& why) { return QStringLiteral("error: ") + why; }

}  // namespace

ResolvedWallpaper resolveWallpaperTarget(const QString& rawTarget, const QString& folder) {
  const QString target = rawTarget.trimmed();
  if (target.isEmpty()) return {{}, QStringLiteral("no file given")};
  const QStringList types = supportedImageTypes();
  if (hasFolderPart(target)) return validateImageFile(target, types);

  if (folder.isEmpty()) {
    return {{},
            QStringLiteral("no wallpaper folder is configured (WallpaperSettings.imagePath); "
                           "give a full path")};
  }
  const QStringList all = collectImageFiles({folder}, types);
  auto find = [&all](const QString& name) {
    QStringList found;
    for (const QString& file : all) {
      if (QFileInfo(file).fileName().compare(name, Qt::CaseInsensitive) == 0) found << file;
    }
    return found;
  };
  QStringList matches = find(target);
  if (matches.isEmpty()) {
    // "sunset" may mean sunset.jpg or sunset.png.
    for (const QString& type : types) matches << find(target + QLatin1Char('.') + type);
  }
  if (matches.size() > 1) {
    QStringList shown;
    for (const QString& match : matches) shown << nativePath(match);
    return {{},
            QStringLiteral("'%1' is ambiguous in %2: %3")
                .arg(target, QDir::toNativeSeparators(folder), shown.join(QStringLiteral("; ")))};
  }
  if (matches.size() == 1) return validateImageFile(matches.first(), types);
  // Not a listed image: a real file of the folder gets the precise reason (wrong type, ...).
  const QString inFolder = QDir(folder).filePath(target);
  if (QFileInfo(inFolder).isFile()) return validateImageFile(inFolder, types);
  return {{},
          QStringLiteral("no wallpaper named '%1' in %2")
              .arg(target, QDir::toNativeSeparators(folder))};
}

Wallpapers::Wallpapers(QObject* parent) : Wallpapers(Deps{}, parent) {}

Wallpapers::Wallpapers(Deps deps, QObject* parent) : QObject(parent), deps_(std::move(deps)) {
  removeRetiredThumbnailFolder();
  if (!deps_.backend && deps_.realBackend && kernel::liveDesktop(QStringLiteral("wallpapers"))) {
    deps_.backend = [] { return makeDesktopWallpaperBackend(); };
  }
  if (!deps_.dryRun) deps_.dryRun = qEnvironmentVariableIntValue("WAKU_WALLPAPER_DRYRUN") != 0;
  if (!deps_.lockScreen && deps_.realBackend && kernel::liveDesktop(QStringLiteral("wallpapers"))) {
    guarded("lock screen", [&] { deps_.lockScreen = makeLockScreenSetter(kernel::appDataDir()); });
  }
  if (deps_.backend) {
    guarded("backend", [&] { backend_ = deps_.backend(); });
  }
  available_ = backend_ != nullptr;
  if (!available_) return;

  if (!deps_.allDesktops && deps_.realBackend) {
    deps_.allDesktops = workspaces::vdesk::setWallpaperForAllDesktops;
  }

  verifyTimer_.setInterval(150);
  connect(&verifyTimer_, &QTimer::timeout, this, [this] {
    guarded("wallpaper check", [&] {
      readCurrent();
      if (changeConfirmed()) return;
      if (++verifyTries_ >= 20) verifyTimer_.stop();
    });
  });

  WallpaperManager::Dependencies managerDeps;
  managerDeps.backend = deps_.backend;
  managerDeps.picker = deps_.picker;
  // A timed change takes the path of a manual random(): lock screen and dry run.
  managerDeps.timedChange = [this] { random(); };
  manager_ = std::make_unique<WallpaperManager>(std::move(managerDeps));
  readCurrent();
}

Wallpapers::~Wallpapers() {
  releaseHotkey();
  if (manager_) manager_->stop();
}

void Wallpapers::releaseHotkey() {
  if (hotkeyHandle_ == 0) return;
  kernel::Hotkeys::instance().remove(hotkeyHandle_);
  hotkeyHandle_ = 0;
}

Wallpapers* Wallpapers::create(QQmlEngine*, QJSEngine*) {
  // The engine owns the returned singleton.
  Wallpapers* service = nullptr;
  if (testFactory()) service = testFactory()().release();
  if (!service) service = new Wallpapers(nullptr);
  return service;
}

void Wallpapers::setFactoryForTests(std::function<std::unique_ptr<Wallpapers>()> factory) {
  testFactory() = std::move(factory);
}

void Wallpapers::setImagePath(const QString& path) {
  if (path == imagePath_) return;
  imagePath_ = path;
  applyToManager();
  emit imagePathChanged();
  refresh();
}

void Wallpapers::configure(const QString& imagePath, const QString& hotkey,
                           const QString& hotkeyScreen, int autoChangeSeconds) {
  guarded("configure", [&] {
    hotkeyScreen_ = hotkeyScreen.isEmpty() ? QStringLiteral("cursor") : hotkeyScreen;
    autoChangeSeconds_ = autoChangeSeconds;
    setImagePath(imagePath);
    applyToManager();
    if (!available_) return;
    if (hotkey == hotkey_) return;
    hotkey_ = hotkey;
    releaseHotkey();
    // Not a live desktop or `--no-hotkeys`: kernel::Hotkeys has no backend, and the hotkey is
    // quietly off, as every GlobalShortcut is.
    kernel::Hotkeys& hotkeys = kernel::Hotkeys::instance();
    if (!hotkeys.active()) return;
    if (hotkey.trimmed().isEmpty()) return;
    const auto combo = kernel::parseHotkey(hotkey);
    if (!combo) {
      qWarning().noquote() << "Wallpapers: cannot parse hotkey" << hotkey;
      return;
    }
    // The callback never outlives this object: the destructor removes the handle.
    hotkeyHandle_ = hotkeys.add(*combo, [this] { onHotkey(); });
    if (hotkeyHandle_ == 0) {
      qWarning().noquote() << "Wallpapers: could not register hotkey" << hotkey
                           << "(already taken?)";
    }
  });
}

void Wallpapers::setSetLockScreen(bool enabled) {
  if (enabled == setLockScreen_) return;
  setLockScreen_ = enabled;
  emit setLockScreenChanged();
}

void Wallpapers::applyToManager() {
  if (!manager_) return;
  manager_->configure(imagePath_.isEmpty() ? QStringList() : QStringList{imagePath_},
                      autoChangeSeconds_, autoChangeSeconds_ > 0, {});
}

void Wallpapers::refresh() {
  guarded("refresh", [&] {
    readCurrent();
    QVariantList list;
    const QString currentKey = normalizedPathKey(current_);
    if (!imagePath_.isEmpty()) {
      for (const QString& file : collectImageFiles({imagePath_})) {
        list.append(QVariantMap{
            {QStringLiteral("path"), file},
            {QStringLiteral("name"), QFileInfo(file).fileName()},
            {QStringLiteral("current"),
             !current_.isEmpty() && normalizedPathKey(file) == currentKey},
        });
      }
    }
    // An unchanged list keeps the gallery's delegates and their loaded thumbnails.
    if (list == files_) return;
    files_ = std::move(list);
    emit filesChanged();
  });
}

void Wallpapers::markCurrent() {
  const QString currentKey = normalizedPathKey(current_);
  bool changed = false;
  for (QVariant& row : files_) {
    QVariantMap map = row.toMap();
    const bool current =
        !current_.isEmpty() &&
        normalizedPathKey(map.value(QStringLiteral("path")).toString()) == currentKey;
    if (map.value(QStringLiteral("current")).toBool() == current) continue;
    map.insert(QStringLiteral("current"), current);
    row = map;
    changed = true;
  }
  if (changed) emit filesChanged();
}

void Wallpapers::readCurrent() {
  if (!backend_) return;
  QString now;
  guarded("current wallpaper", [&] {
    const auto monitors = backend_->monitors();
    now = backend_->wallpaperOf(monitors.empty() ? QString() : monitors.front().id);
  });
  if (now == current_) return;
  current_ = now;
  markCurrent();
  emit currentChanged();
}

void Wallpapers::setWallpaper(const QString& path) { setWallpaperImpl(path, false); }

bool Wallpapers::setWallpaperAllDesktops(const QString& path) {
  if (!available_) return false;
  bool applied = false;
  guarded("set wallpaper on all desktops", [&] {
    const ResolvedWallpaper resolved = resolveWallpaperTarget(path, imagePath_);
    if (!resolved.ok()) {
      qWarning().noquote() << "Wallpapers:" << resolved.error;
      return;
    }
    if (deps_.dryRun) {
      qInfo().noquote() << "Wallpapers: dry run, not setting on all desktops" << resolved.path;
      updateLockScreen(resolved.path, true);
      applied = true;
      return;
    }
    if (!deps_.allDesktops || !deps_.allDesktops(resolved.path)) {
      qWarning() << "Wallpapers: could not set wallpaper on all desktops";
      return;
    }
    readCurrent();
    confirmChange(resolved.path, QString());
    applied = true;
  });
  return applied;
}

void Wallpapers::setWallpaperImpl(const QString& path, bool dryRun) {
  if (!available_ || !QFileInfo(path).isFile()) return;
  guarded("set wallpaper", [&] { commitWallpaper(path, dryRun); });
}

void Wallpapers::commitWallpaper(const QString& path, bool dryRun) {
  if (deps_.dryRun || dryRun) {
    qInfo().noquote() << "Wallpapers: dry run, not setting" << path;
    updateLockScreen(path, true);
    return;
  }
  manager_->changeBackground(path);
  // The manager has just listed the folder; only the current mark moves.
  readCurrent();
  // `current` comes from Windows, so a failed set leaves the lock screen alone.
  confirmChange(path, QString());
}

void Wallpapers::confirmChange(const QString& path, const QString& beforeKey) {
  verifyTimer_.stop();
  verifyPath_ = path;
  verifyBeforeKey_ = beforeKey;
  verifyTries_ = 0;
  if (!changeConfirmed()) verifyTimer_.start();
}

bool Wallpapers::changeConfirmed() {
  if (current_.isEmpty()) return false;
  const QString key = normalizedPathKey(current_);
  const bool done =
      verifyPath_.isEmpty() ? key != verifyBeforeKey_ : key == normalizedPathKey(verifyPath_);
  if (!done) return false;
  verifyTimer_.stop();
  updateLockScreen(verifyPath_.isEmpty() ? current_ : verifyPath_, false);
  return true;
}

void Wallpapers::updateLockScreen(const QString& path, bool dryRun) {
  if (!setLockScreen_) return;
  guarded("lock screen", [&] {
    if (const auto reason = lockScreenSkipReason(path)) {
      qInfo().noquote() << "Wallpapers: lock screen left alone," << *reason << ":"
                        << nativePath(path);
      return;
    }
    if (dryRun) {
      qInfo().noquote() << "Wallpapers: lock screen (dry run):" << nativePath(path);
      return;
    }
    if (deps_.lockScreen) deps_.lockScreen->set(nativePath(path));
  });
}

void Wallpapers::random() {
  if (!available_) return;
  guarded("random wallpaper", [&] {
    // The manager sets what it picks, so a dry run picks here too.
    if (!deps_.dryRun) {
      const QString before = normalizedPathKey(current_);
      manager_->changeBackground();
      refresh();
      confirmChange(QString(), before);
      return;
    }
    // A dry run picks here (never the one shown now) and goes the way of a gallery click.
    const QStringList all = collectImageFiles({imagePath_});
    if (all.isEmpty()) return;
    QString next;
    do {
      const int count = static_cast<int>(all.size());
      next = all.at(deps_.picker ? deps_.picker(count) % count
                                 : static_cast<int>(QRandomGenerator::global()->bounded(count)));
    } while (all.size() > 1 && normalizedPathKey(next) == normalizedPathKey(current_) &&
             (deps_.picker == nullptr));
    setWallpaper(next);
  });
}

// --- wakuc wallpaper ...

QString Wallpapers::controlStart(const QString& path, bool dryRun) {
  setWallpaperImpl(path, dryRun);
  return okAnswer(path);
}

QString Wallpapers::controlSet(const QString& target, bool dryRun) {
  QString answer;
  guarded("wallpaper set", [&] {
    if (!available_) {
      answer = errorAnswer(QStringLiteral("the wallpaper service is not available"));
      return;
    }
    const ResolvedWallpaper resolved = resolveWallpaperTarget(target, imagePath_);
    answer = resolved.ok() ? controlStart(resolved.path, dryRun) : errorAnswer(resolved.error);
  });
  return answer.isEmpty() ? errorAnswer(QStringLiteral("the wallpaper could not be set")) : answer;
}

QString Wallpapers::controlRandom(bool dryRun) {
  QString answer;
  guarded("wallpaper random", [&] {
    if (!available_) {
      answer = errorAnswer(QStringLiteral("the wallpaper service is not available"));
      return;
    }
    readCurrent();
    const QStringList all = collectImageFiles({imagePath_});
    if (all.isEmpty()) {
      answer = errorAnswer(QStringLiteral("no wallpapers found in %1")
                               .arg(imagePath_.isEmpty() ? QStringLiteral("(no folder configured)")
                                                         : QDir::toNativeSeparators(imagePath_)));
      return;
    }
    // Never the one shown now, unless it is the only one.
    const QString currentKey = normalizedPathKey(current_);
    QStringList candidates;
    for (const QString& file : all) {
      if (normalizedPathKey(file) != currentKey) candidates << file;
    }
    if (candidates.isEmpty()) candidates = all;
    const int count = static_cast<int>(candidates.size());
    const int index = deps_.picker ? deps_.picker(count) % count
                                   : static_cast<int>(QRandomGenerator::global()->bounded(count));
    answer = controlStart(nativePath(candidates.at(index)), dryRun);
  });
  return answer.isEmpty() ? errorAnswer(QStringLiteral("the wallpaper could not be set")) : answer;
}

// step +1 is the file after the current one, -1 the one before; both wrap around. When the
// current wallpaper is not in the folder, next is the first file and previous the last.
QString Wallpapers::controlStep(int step, bool dryRun) {
  QString answer;
  guarded("wallpaper step", [&] {
    if (!available_) {
      answer = errorAnswer(QStringLiteral("the wallpaper service is not available"));
      return;
    }
    readCurrent();
    const QStringList all = collectImageFiles({imagePath_});
    if (all.isEmpty()) {
      answer = errorAnswer(QStringLiteral("no wallpapers found in %1")
                               .arg(imagePath_.isEmpty() ? QStringLiteral("(no folder configured)")
                                                         : QDir::toNativeSeparators(imagePath_)));
      return;
    }
    const int count = static_cast<int>(all.size());
    const QString currentKey = normalizedPathKey(current_);
    int at = -1;
    for (int i = 0; i < count && at < 0; ++i) {
      if (normalizedPathKey(all.at(i)) == currentKey) at = i;
    }
    const int target = at < 0 ? (step > 0 ? 0 : count - 1) : (at + step + count) % count;
    answer = controlStart(nativePath(all.at(target)), dryRun);
  });
  return answer.isEmpty() ? errorAnswer(QStringLiteral("the wallpaper could not be set")) : answer;
}

QString Wallpapers::controlNext(bool dryRun) { return controlStep(1, dryRun); }
QString Wallpapers::controlPrevious(bool dryRun) { return controlStep(-1, dryRun); }

QString Wallpapers::controlGallery() {
  if (!available_) return errorAnswer(QStringLiteral("the wallpaper service is not available"));
  toggleGallery();
  return okAnswer(galleryOpen_ ? QStringLiteral("gallery opened")
                               : QStringLiteral("gallery closed"));
}

QString Wallpapers::screenNameFor(const QString& mode) const {
  QScreen* chosen = nullptr;
  if (mode == QLatin1String("cursor")) {
    chosen = QGuiApplication::screenAt(QCursor::pos());
  } else if (mode == QLatin1String("active")) {
    if (const auto monitor = foregroundMonitorRect()) {
      for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->geometry().topLeft() == monitor->topLeft()) chosen = screen;
      }
    }
  }
  if (!chosen) chosen = QGuiApplication::primaryScreen();
  return chosen ? chosen->name() : QString();
}

void Wallpapers::toggleGallery() {
  if (galleryOpen_) {
    closeGallery();
    return;
  }
  if (!available_) return;
  guarded("gallery", [&] {
    refresh();
    galleryScreen_ = screenNameFor(hotkeyScreen_);
    galleryOpen_ = true;
    emit galleryOpenChanged();
  });
}

void Wallpapers::closeGallery() {
  if (!galleryOpen_) return;
  galleryOpen_ = false;
  emit galleryOpenChanged();
}

void Wallpapers::focusWindow(QWindow* window) {
  if (!window || !waku::kernel::liveDesktop(QStringLiteral("wallpapers"))) return;
  window->requestActivate();
  waku::kernel::forceForegroundFocus(static_cast<std::uintptr_t>(window->winId()));
}

}  // namespace waku::wallpapers

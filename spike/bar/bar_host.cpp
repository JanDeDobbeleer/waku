#include "spike/bar/bar_host.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QUrl>

#include "spike/bar/screen_match.h"

namespace barnacle {

namespace {

constexpr auto kWindowUrl = "qrc:/qt/qml/barnacle/BarWindow.qml";
constexpr auto kFallbackUrl = "qrc:/qt/qml/barnacle/FallbackConfig.qml";

// "file:line:col: message", with the file name only.
QString describe(const QQmlError& error) {
  return QStringLiteral("%1:%2:%3: %4")
      .arg(QFileInfo(error.url().toLocalFile().isEmpty() ? error.url().fileName()
                                                         : error.url().toLocalFile())
               .fileName())
      .arg(error.line())
      .arg(error.column())
      .arg(error.description());
}

QStringList describeAll(const QList<QQmlError>& errors) {
  QStringList lines;
  for (const QQmlError& error : errors) lines << describe(error);
  return lines;
}

QList<QScreen*> applicationScreens() { return QGuiApplication::screens(); }

yasb::bar::AppBarEdge edgeOf(Bar::Position position) {
  return position == Bar::Bottom ? yasb::bar::AppBarEdge::Bottom : yasb::bar::AppBarEdge::Top;
}

}  // namespace

BarHost::BarHost(QQmlEngine* engine, Options options, QObject* parent)
    : QObject(parent), engine_(engine), options_(std::move(options)) {
  if (!options_.screens) options_.screens = applicationScreens;
  windowComponent_ =
      std::make_unique<QQmlComponent>(engine_, QUrl(QString::fromLatin1(kWindowUrl)));
  if (windowComponent_->isError()) {
    qCritical().noquote() << "barnacle: cannot load the bar window:"
                          << describeAll(windowComponent_->errors()).join('\n');
  }
  debounce_.setSingleShot(true);
  debounce_.setInterval(150);
  connect(&debounce_, &QTimer::timeout, this, [this] {
    reload();
    refreshWatches();
  });
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, &debounce_,
          qOverload<>(&QTimer::start));
  connect(&watcher_, &QFileSystemWatcher::fileChanged, &debounce_, qOverload<>(&QTimer::start));
}

BarHost::~BarHost() {
  // Windows (and their app bars) go before the config object and component they refer to.
  loaded_.reset();
}

QString BarHost::entryPath() const {
  return QDir(options_.configHome).filePath(options_.entryFile);
}

QString BarHost::themePath() const {
  return QDir(options_.configHome).filePath(options_.themeFile);
}

QList<QQuickWindow*> BarHost::windows() const {
  QList<QQuickWindow*> list;
  if (loaded_) {
    for (const Entry& entry : loaded_->entries) list << entry.window;
  }
  return list;
}

Config* BarHost::config() const { return loaded_ ? loaded_->config : nullptr; }

void BarHost::setError(const QString& error) {
  if (error == error_) return;
  error_ = error;
  emit errorChanged();
}

bool BarHost::validate(const QUrl& url, const QMetaObject& expected, QString* error) {
  // A throwaway engine: no cache can serve a stale file, and nothing the current bars use is
  // touched. The layouts are lazy components, so creating the config builds no widgets and starts
  // no service; it only proves the file compiles, its types exist and its root is a Config.
  QQmlEngine probe;
  probe.setImportPathList(engine_->importPathList());
  QQmlComponent component(&probe, url);
  if (component.isError()) {
    const QStringList lines = describeAll(component.errors());
    qWarning().noquote() << "barnacle: config errors:\n" << lines.join('\n');
    *error = lines.value(0);
    return false;
  }
  std::unique_ptr<QObject> root(component.create());
  if (!root) {
    const QStringList lines = describeAll(component.errors());
    qWarning().noquote() << "barnacle: config errors:\n" << lines.join('\n');
    *error = lines.value(0, QStringLiteral("the config could not be created"));
    return false;
  }
  if (!root->metaObject()->inherits(&expected)) {
    *error = QStringLiteral("%1 must declare a %2 at its root")
                 .arg(QFileInfo(url.toLocalFile()).fileName(),
                      QString::fromLatin1(expected.className()).section(QStringLiteral("::"), -1));
    qWarning().noquote() << "barnacle:" << *error;
    return false;
  }
  return true;
}

bool BarHost::validateTheme(QString* error) {
  if (!QFileInfo::exists(themePath())) return true;
  return validate(QUrl::fromLocalFile(themePath()), ThemeSpec::staticMetaObject, error);
}

void BarHost::applyTheme() {
  auto* theme =
      engine_->singletonInstance<Theme*>(QStringLiteral("barnacle"), QStringLiteral("Theme"));
  if (!theme) return;
  const ThemeSpec defaults;
  if (!QFileInfo::exists(themePath())) {
    theme->assignFrom(defaults);
    return;
  }
  // Declared in this order so the object is destroyed before its component.
  QQmlComponent component(engine_, QUrl::fromLocalFile(themePath()));
  std::unique_ptr<QObject> root(component.isError() ? nullptr : component.create());
  if (auto* spec = qobject_cast<ThemeSpec*>(root.get())) {
    theme->assignFrom(*spec);
  } else {
    theme->assignFrom(defaults);
  }
}

bool BarHost::reload() {
  QString error;
  std::unique_ptr<Loaded> next;
  if (!QFileInfo::exists(entryPath())) {
    error = QStringLiteral("%1 not found in %2")
                .arg(options_.entryFile, QDir::toNativeSeparators(options_.configHome));
    qWarning().noquote() << "barnacle:" << error;
  } else if (validate(QUrl::fromLocalFile(entryPath()), Config::staticMetaObject, &error) &&
             validateTheme(&error)) {
    // The config is good, so the old bars can go. Qt only reloads changed files when no object
    // made from the old ones is alive (see QQmlEngine::clearComponentCache).
    loaded_.reset();
    engine_->clearComponentCache();
    applyTheme();
    next = build(QUrl::fromLocalFile(entryPath()), &error);
  }

  if (!next) {
    setError(error);
    if (!loaded_) {
      // Nothing to keep: show the fallback bar so the error is visible.
      QString fallbackError;
      if (auto fallback = build(QUrl(QString::fromLatin1(kFallbackUrl)), &fallbackError)) {
        install(std::move(fallback));
      } else {
        qCritical().noquote() << "barnacle: the fallback bar failed:" << fallbackError;
      }
    }
    emit reloaded(false);
    return false;
  }

  setError({});
  install(std::move(next));
  emit reloaded(true);
  return true;
}

std::unique_ptr<BarHost::Loaded> BarHost::build(const QUrl& url, QString* error) {
  auto loaded = std::make_unique<Loaded>();
  loaded->component = std::make_unique<QQmlComponent>(engine_, url);
  if (loaded->component->isError()) {
    const QStringList lines = describeAll(loaded->component->errors());
    qWarning().noquote() << "barnacle: config errors:\n" << lines.join('\n');
    *error = lines.value(0);
    return nullptr;
  }
  loaded->root.reset(loaded->component->create());
  if (!loaded->root) {
    const QStringList lines = describeAll(loaded->component->errors());
    qWarning().noquote() << "barnacle: config errors:\n" << lines.join('\n');
    *error = lines.value(0, QStringLiteral("the config could not be created"));
    return nullptr;
  }
  loaded->config = qobject_cast<Config*>(loaded->root.get());
  if (!loaded->config) {
    *error = QStringLiteral("%1 must declare a Config at its root").arg(options_.entryFile);
    qWarning().noquote() << "barnacle:" << *error;
    return nullptr;
  }
  if (!windowComponent_ || windowComponent_->isError()) {
    *error = QStringLiteral("the bar window component is broken");
    return nullptr;
  }

  const QList<QScreen*> screens = options_.screens();
  std::vector<ScreenRef> refs;
  QScreen* primary = QGuiApplication::primaryScreen();
  for (QScreen* screen : screens) refs.push_back({screen->name(), screen == primary});

  for (Bar* bar : loaded->config->barList()) {
    if (!bar->enabled()) continue;
    for (int index : matchScreens(bar->screens(), refs)) {
      QScreen* screen = screens.at(index);
      std::unique_ptr<QObject> object(windowComponent_->createWithInitialProperties(
          {{QStringLiteral("screen"), QVariant::fromValue(screen)},
           {QStringLiteral("spec"), QVariant::fromValue(bar)},
           {QStringLiteral("host"), QVariant::fromValue(static_cast<QObject*>(this))}}));
      auto* window = qobject_cast<QQuickWindow*>(object.get());
      if (!window) {
        const QStringList lines = describeAll(windowComponent_->errors());
        qWarning().noquote() << "barnacle: cannot create a bar window:\n" << lines.join('\n');
        *error = lines.value(0, QStringLiteral("a bar window could not be created"));
        return nullptr;
      }
      Entry entry;
      entry.window = window;
      entry.object = std::move(object);
      entry.screen = screen;
      entry.spec = bar;
      loaded->entries.push_back(std::move(entry));
    }
  }
  return loaded;
}

void BarHost::install(std::unique_ptr<Loaded> loaded) {
  // The new windows exist but are hidden; drop the old ones first so two bars never overlap.
  loaded_.reset();
  loaded_ = std::move(loaded);
  for (Entry& entry : loaded_->entries) present(entry);
}

void BarHost::present(Entry& entry) {
  entry.window->show();
  const auto hwnd = static_cast<quintptr>(entry.window->winId());
  const NativeHooks& hooks = options_.hooks;

  if (hooks.applyBackdrop) hooks.applyBackdrop(hwnd, entry.spec->backdrop());
  if (hooks.monitorHandle) {
    entry.window->setProperty("monitorHandle", hooks.monitorHandle(hwnd));
  }
  if (hooks.makeAppBar) {
    entry.appBar = hooks.makeAppBar();
    yasb::bar::AppBarRequest request;
    request.hwnd = hwnd;
    request.edge = edgeOf(entry.spec->position());
    request.height = entry.spec->height();
    request.screen = entry.screen;
    request.reserveSpace = options_.allowReserve && entry.spec->reserveSpace();
    request.alwaysOnTop = true;
    entry.appBar->create(request);
  }
}

void BarHost::watch() {
  watching_ = true;
  refreshWatches();
}

void BarHost::refreshWatches() {
  if (!watching_) return;
  // A saved file is often replaced, so every path is added again after each change.
  QStringList paths{options_.configHome};
  QDirIterator it(options_.configHome, {QStringLiteral("*.qml"), QStringLiteral("qmldir")},
                  QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) paths << it.next();
  const QStringList old = watcher_.files() + watcher_.directories();
  if (!old.isEmpty()) watcher_.removePaths(old);
  watcher_.addPaths(paths);
}

}  // namespace barnacle

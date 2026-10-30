#include "host/qml/shell_host.h"

#include <private/qqmlcomponent_p.h>
#include <private/qqmlpropertycache_p.h>
#include <private/qv4executablecompilationunit_p.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QUrl>
#include <QWindow>

#include "host/plugin_guard.h"
#include "host/qml/shell_screen.h"
#include "host/qml/shell_window.h"

namespace wabar::host {

namespace {

constexpr auto kFallbackUrl = "qrc:/qt/qml/wabar/FallbackShell.qml";
constexpr auto kSafeUrl = "qrc:/qt/qml/wabar/widgets/SafeShell.qml";
// Loaded once so the wabar types are registered before the host asks for its singletons.
constexpr auto kChromeUrl = "qrc:/qt/qml/wabar/WindowChrome.qml";
constexpr auto kHostProperty = "ShellHost";

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

// The first C++ type the root object of a compiled component is, without creating anything.
// Qt has no public API for it; the private one is stable across Qt 6 (the compilation unit's root
// property cache). Null when the component did not compile.
const QMetaObject* rootType(QQmlComponent& component) {
  QQmlComponentPrivate* d = QQmlComponentPrivate::get(&component);
  if (!d || !d->compilationUnit()) return nullptr;
  const QQmlPropertyCache::ConstPtr cache = d->compilationUnit()->rootPropertyCache();
  return cache ? cache->firstCppMetaObject() : nullptr;
}

// Every part of a shell: the objects declared in its root and nested Scopes that are not
// windows, Scopes or Variants and LazyLoaders (those make windows, not parts).
void collectParts(const Scope* scope, QList<QObject*>* parts) {
  for (QObject* object : scope->children()) {
    if (auto* nested = qobject_cast<Scope*>(object)) {
      collectParts(nested, parts);
    } else if (!qobject_cast<QsWindow*>(object)) {
      *parts << object;
    }
  }
}

}  // namespace

// Qt keeps the compiled form of a QML file for as long as an object made from it lives, and
// clearComponentCache() does not drop it. A reload builds the new shell while the old one still
// stands, so without this it would be made from the old files. A changing query on the URL makes
// each reload's files new to the engine. The interceptor only touches QML files under the config
// home.
class ShellHost::GenerationInterceptor : public QQmlAbstractUrlInterceptor {
 public:
  explicit GenerationInterceptor(QString root) : root_(std::move(root)) {}
  void nextGeneration() { ++generation_; }

  QUrl intercept(const QUrl& url, DataType type) override {
    if (generation_ == 0 || type != QmlFile || !url.isLocalFile()) return url;
    if (!url.toLocalFile().startsWith(root_, Qt::CaseInsensitive)) return url;
    QUrl versioned = url;
    versioned.setQuery(QStringLiteral("wabar=%1").arg(generation_));
    return versioned;
  }

 private:
  QString root_;
  int generation_ = 0;
};

ShellHost::Loaded::~Loaded() {
  // Take the app bars off the desktop and the windows off the screen now: a Variants destroys its
  // windows later (deleteLater), and the next shell must not overlap the old one.
  for (Entry& entry : entries) {
    if (entry.appBar) entry.appBar->remove();
    if (entry.window) entry.window->setPresentable(false);
  }
  entries.clear();
}

ShellHost::ShellHost(QQmlEngine* engine, Options options, QObject* parent)
    : QObject(parent), engine_(engine), options_(std::move(options)) {
  if (!options_.screens) options_.screens = applicationScreens;
  engine_->rootContext()->setContextProperty(QString::fromLatin1(kHostProperty), this);
  if (options_.hooks.cursorPosition) {
    maskHub_ = std::make_unique<MaskPollerHub>(options_.hooks.cursorPosition);
  }
  interceptor_ = std::make_unique<GenerationInterceptor>(
      QDir::fromNativeSeparators(QDir(options_.configHome).absolutePath()));
  engine_->addUrlInterceptor(interceptor_.get());
  {
    // Registers the wabar types (static plugin) so singletonInstance() finds Theme and ScreenList.
    QQmlComponent chrome(engine_, QUrl(QString::fromLatin1(kChromeUrl)));
    if (chrome.isError()) {
      qCritical().noquote() << "wabar: cannot load the window chrome:"
                            << describeAll(chrome.errors()).join('\n');
    }
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
  if (QCoreApplication::instance()) QCoreApplication::instance()->installEventFilter(this);
}

ShellHost::~ShellHost() {
  if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
  // Windows (and their app bars) go before the root object and component they refer to.
  loaded_.reset();
  engine_->removeUrlInterceptor(interceptor_.get());
  if (engine_->rootContext()
          ->contextProperty(QString::fromLatin1(kHostProperty))
          .value<QObject*>() == this) {
    engine_->rootContext()->setContextProperty(QString::fromLatin1(kHostProperty),
                                               static_cast<QObject*>(nullptr));
  }
}

ShellHost* ShellHost::of(QQmlEngine* engine) {
  if (!engine) return nullptr;
  return qobject_cast<ShellHost*>(
      engine->rootContext()->contextProperty(QString::fromLatin1(kHostProperty)).value<QObject*>());
}

bool ShellHost::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::TouchBegin) {
    return false;
  }
  auto* window = qobject_cast<QQuickWindow*>(watched);
  if (!window || !loaded_) return false;
  for (const Entry& entry : loaded_->entries) {
    if (entry.window && entry.window->backingWindow() == window) {
      emit windowPressed();
      break;
    }
  }
  return false;  // observe only
}

QString ShellHost::entryPath() const {
  return QDir(options_.configHome).filePath(options_.entryFile);
}

QString ShellHost::themePath() const {
  return QDir(options_.configHome).filePath(options_.themeFile);
}

QList<QQuickWindow*> ShellHost::windows() const {
  QList<QQuickWindow*> list;
  if (loaded_) {
    for (const Entry& entry : loaded_->entries) {
      if (entry.presented && entry.window && entry.window->backingWindowVisible()) {
        list << entry.window->backingWindow();
      }
    }
  }
  return list;
}

ShellRoot* ShellHost::shell() const { return loaded_ ? loaded_->shell : nullptr; }

void ShellHost::setError(const QString& error) {
  if (error == error_) return;
  error_ = error;
  emit errorChanged();
}

bool ShellHost::validate(const QUrl& url, const QMetaObject& expected, bool create,
                         QString* error) {
  // A throwaway engine: no cache can serve a stale file, and nothing the current shell uses is
  // touched. Compiling resolves every import and type of the file and of the files it uses;
  // nothing is created, so no window, widget or service starts.
  QQmlEngine probe;
  probe.setImportPathList(engine_->importPathList());
  QQmlComponent component(&probe, url);
  if (component.isError()) {
    const QStringList lines = describeAll(component.errors());
    qWarning().noquote() << "wabar: config errors:\n" << lines.join('\n');
    *error = lines.value(0);
    return false;
  }
  const QMetaObject* root = rootType(component);
  if (!root || !root->inherits(&expected)) {
    *error = QStringLiteral("%1 must declare a %2 at its root")
                 .arg(QFileInfo(url.toLocalFile()).fileName(),
                      QString::fromLatin1(expected.className()).section(QStringLiteral("::"), -1));
    qWarning().noquote() << "wabar:" << *error;
    return false;
  }
  if (create) {
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
      const QStringList lines = describeAll(component.errors());
      qWarning().noquote() << "wabar: config errors:\n" << lines.join('\n');
      *error = lines.value(0, QStringLiteral("the file could not be created"));
      return false;
    }
  }
  return true;
}

bool ShellHost::validateTheme(QString* error) {
  if (!QFileInfo::exists(themePath())) return true;
  return validate(QUrl::fromLocalFile(themePath()), ThemeSpec::staticMetaObject, true, error);
}

void ShellHost::applyTheme() {
  auto* theme =
      engine_->singletonInstance<Theme*>(QStringLiteral("wabar"), QStringLiteral("Theme"));
  if (!theme) return;
  const ThemeSpec defaults;
  if (options_.safe || !QFileInfo::exists(themePath())) {
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

QUrl ShellHost::safeShellUrl() { return QUrl(QString::fromLatin1(kSafeUrl)); }

bool ShellHost::checkShell(const QUrl& url, QString* error) {
  return validate(url, ShellRoot::staticMetaObject, false, error);
}

bool ShellHost::buildFallback() {
  QString fallbackError;
  if (auto fallback = build(QUrl(QString::fromLatin1(kFallbackUrl)), &fallbackError)) {
    install(std::move(fallback));
    return true;
  }
  qCritical().noquote() << "wabar: the fallback shell failed:" << fallbackError;
  return false;
}

QString ShellHost::failures(const Loaded& loaded, int* built) {
  QStringList errors;
  *built = 0;
  for (const Entry& entry : loaded.entries) {
    if (!entry.window) continue;
    if (entry.window->buildError().isEmpty()) {
      ++*built;
    } else {
      errors << entry.window->buildError();
    }
  }
  if (errors.isEmpty()) return {};
  return errors.size() == 1
             ? errors.first()
             : QStringLiteral("%1 windows failed: %2").arg(errors.size()).arg(errors.first());
}

bool ShellHost::reloadSafe() {
  // Nothing in the config home is read: the built-in shell and the default theme, always.
  loaded_.reset();
  engine_->clearComponentCache();
  applyTheme();
  QString error;
  std::unique_ptr<Loaded> next =
      build(options_.safeUrl.isValid() ? options_.safeUrl : safeShellUrl(), &error);
  if (!next) {
    setError(error);
    buildFallback();
    emit reloaded(false);
    return false;
  }
  setError({});
  install(std::move(next));
  emit reloaded(true);
  return true;
}

bool ShellHost::reload() {
  if (options_.safe) return reloadSafe();
  QString error;
  std::unique_ptr<Loaded> next;
  const QString legacyPath = QDir(options_.configHome).filePath(options_.legacyEntryFile);
  if (!QFileInfo::exists(entryPath())) {
    if (!options_.legacyEntryFile.isEmpty() && QFileInfo::exists(legacyPath)) {
      error = QStringLiteral(
                  "%1 is the wabar 1 format: rename it to %2 and its Config to ShellRoot, Bar to "
                  "Panel, BarLayout to PanelLayout (see docs/building.md)")
                  .arg(options_.legacyEntryFile, options_.entryFile);
    } else {
      error = QStringLiteral("%1 not found in %2")
                  .arg(options_.entryFile, QDir::toNativeSeparators(options_.configHome));
    }
    qWarning().noquote() << "wabar:" << error;
  } else if (const QList<PluginFailure> refused =
                 configPluginFailures(options_.configHome, engine_->importPathList());
             !refused.isEmpty()) {
    // A qmldir outside the guarded import roots (beside shell.qml, under `import "./lib"`) names
    // a plugin that does not fit this process: loading it could crash the bar.
    error = QStringLiteral("plugin %1 refused: %2")
                .arg(QDir::toNativeSeparators(refused.first().file), refused.first().reason);
    qWarning().noquote() << "wabar:" << error;
  } else if (validate(QUrl::fromLocalFile(entryPath()), ShellRoot::staticMetaObject, false,
                      &error) &&
             validateTheme(&error)) {
    // The files compile, but creating the shell can still fail. The old shell stays alive and
    // on screen while the new one is built (hidden); install() swaps them on success.
    interceptor_->nextGeneration();
    engine_->clearComponentCache();
    applyTheme();
    next = build(QUrl::fromLocalFile(entryPath()), &error);
  }

  if (next) {
    int built = 0;
    const QString failed = failures(*next, &built);
    const bool hadWindows = loaded_ && !loaded_->entries.empty();
    if (!failed.isEmpty() && built == 0) {
      // Not one window of the new shell could be built: nothing to show but the error.
      error = failed;
      qWarning().noquote() << "wabar: no window could be built:" << error;
      next.reset();
    } else if (next->entries.empty() && hadWindows) {
      // A shell that makes no windows where the old one had some is a failed delegate or a
      // wrong filter far more often than a wish; keep what works.
      error =
          QStringLiteral("%1 created no windows; the previous shell stays").arg(options_.entryFile);
      qWarning().noquote() << "wabar:" << error;
      next.reset();
    } else {
      setError(failed);
      install(std::move(next));
      emit reloaded(true);
      return true;
    }
  }

  setError(error);
  if (!loaded_) buildFallback();  // nothing to keep: show the error
  emit reloaded(false);
  return false;
}

std::unique_ptr<ShellHost::Loaded> ShellHost::build(const QUrl& url, QString* error) {
  if (ScreenList* screens = ScreenList::of(engine_)) {
    screens->setScreens(options_.screens(), QGuiApplication::primaryScreen());
  }
  auto loaded = std::make_unique<Loaded>();
  loaded->component = std::make_unique<QQmlComponent>(engine_, url);
  if (loaded->component->isError()) {
    const QStringList lines = describeAll(loaded->component->errors());
    qWarning().noquote() << "wabar: config errors:\n" << lines.join('\n');
    *error = lines.value(0);
    return nullptr;
  }
  // Windows register while the shell is created (QsWindow::componentComplete); install() shows
  // them once the whole shell exists.
  building_ = loaded.get();
  loaded->root.reset(loaded->component->create());
  building_ = nullptr;
  if (!loaded->root) {
    const QStringList lines = describeAll(loaded->component->errors());
    qWarning().noquote() << "wabar: config errors:\n" << lines.join('\n');
    *error = lines.value(0, QStringLiteral("the shell could not be created"));
    return nullptr;
  }
  loaded->shell = qobject_cast<ShellRoot*>(loaded->root.get());
  if (!loaded->shell) {
    *error = QStringLiteral("%1 must declare a ShellRoot at its root")
                 .arg(QFileInfo(url.toLocalFile().isEmpty() ? url.fileName() : url.toLocalFile())
                          .fileName());
    qWarning().noquote() << "wabar:" << *error;
    return nullptr;
  }
  return loaded;
}

void ShellHost::install(std::unique_ptr<Loaded> loaded) {
  // The new windows exist but are hidden; drop the old ones first so two shells never overlap.
  loaded_.reset();
  loaded_ = std::move(loaded);
  for (Entry& entry : loaded_->entries) present(entry);
  // Objects declared next to the windows start work now that the shell is the live one.
  QList<QObject*> parts;
  if (loaded_->shell) collectParts(loaded_->shell, &parts);
  for (QObject* part : parts) {
    if (part->metaObject()->indexOfMethod("activate()") < 0) continue;
    try {
      QMetaObject::invokeMethod(part, "activate");
    } catch (const std::exception& e) {
      qWarning().noquote() << "wabar: a shell part failed to start:" << e.what();
    } catch (...) {
      qWarning().noquote() << "wabar: a shell part failed to start";
    }
  }
}

void ShellHost::adopt(QsWindow* window) {
  Loaded* target = building_ ? building_ : loaded_.get();
  if (!target) {
    // No shell of this host is being built or shown (a window made outside reload()).
    window->setPresentable(true);
    return;
  }
  target->entries.push_back(Entry{window, nullptr, false});
  if (target == loaded_.get()) {
    // Made after install, by a Variants or a LazyLoader: shown at once.
    if (!window->buildError().isEmpty()) setError(window->buildError());
    present(target->entries.back());
  }
}

void ShellHost::release(QsWindow* window) {
  for (Loaded* loaded : {building_, loaded_.get()}) {
    if (!loaded) continue;
    for (auto it = loaded->entries.begin(); it != loaded->entries.end(); ++it) {
      if (it->window != window) continue;
      if (it->appBar) it->appBar->remove();
      loaded->entries.erase(it);
      return;
    }
  }
}

QList<QsWindow*> ShellHost::namedWindows(const QString& name) const {
  QList<QsWindow*> list;
  if (!loaded_) return list;
  for (const Entry& entry : loaded_->entries) {
    QsWindow* window = entry.window;
    if (!window || !entry.presented || window->name().isEmpty()) continue;
    if (name.isEmpty() || window->name() == name) list << window;
  }
  return list;
}

void ShellHost::noteVisibility(const QString& name, bool visible) { visibility_[name] = visible; }

void ShellHost::windowShown(QsWindow* window, bool activate) {
  const NativeHooks& hooks = options_.hooks;
  QQuickWindow* backing = window->backingWindow();
  const auto hwnd = static_cast<quintptr>(backing->winId());
  try {
    if (hooks.setNoActivate) hooks.setNoActivate(hwnd, !window->takesFocus());
    if (window->stackTopmost() && hooks.raiseTopmost) hooks.raiseTopmost(hwnd);
    if (activate) {
      backing->requestActivate();
      if (hooks.forceForeground) hooks.forceForeground(hwnd);
    }
  } catch (const std::exception& e) {
    qWarning().noquote() << "wabar: stacking a window failed:" << e.what();
  } catch (...) {
    qWarning().noquote() << "wabar: stacking a window failed";
  }
}

void ShellHost::windowStackingChanged(QsWindow* window) { windowShown(window, false); }

void ShellHost::present(Entry& entry) {
  QsWindow* window = entry.window;
  if (!window || !window->buildError().isEmpty()) return;
  entry.presented = true;
  // A window the user showed or hid before a reload is in the same state after it. The shell is not
  // live yet, so this neither takes the keyboard focus nor is noted again.
  if (const auto it = visibility_.constFind(window->name());
      !window->name().isEmpty() && it != visibility_.constEnd()) {
    window->setVisible(it.value());
  }
  if (window->backingWindowVisible()) {
    applyNative(entry);
    return;
  }
  // The native look needs a window handle, so it waits for the first show (a window declared
  // with `visible: false` gets it when it is first shown).
  QsWindow* raw = window;
  connect(raw, &QsWindow::firstShown, this, [this, raw] {
    if (!loaded_) return;
    for (Entry& candidate : loaded_->entries) {
      if (candidate.window == raw) applyNative(candidate);
    }
  });
  window->setPresentable(true);
}

void ShellHost::applyNative(Entry& entry) {
  QsWindow* window = entry.window;
  if (!window || entry.appBar) return;
  QQuickWindow* backing = window->backingWindow();
  const auto hwnd = static_cast<quintptr>(backing->winId());
  const NativeHooks& hooks = options_.hooks;
  try {
    if (hooks.applyBackdrop) hooks.applyBackdrop(hwnd, window->backdrop());
    if (hooks.monitorHandle) window->setMonitorHandle(hooks.monitorHandle(hwnd));
    auto* panel = qobject_cast<PanelWindow*>(window);
    if (!panel || !hooks.makeAppBar) return;
    const std::optional<AppBarEdge> edge = panel->appBarEdge();
    if (!edge) return;
    const int zone = panel->reservedZone();
    entry.appBar = hooks.makeAppBar();
    wabar::host::AppBarRequest request;
    request.hwnd = hwnd;
    request.edge = *edge;
    request.thickness =
        zone > 0 ? zone : panelThickness(*edge, QSize(panel->width(), panel->height()));
    request.screen = window->targetScreen();
    request.reserveSpace = options_.allowReserve && zone > 0;
    request.alwaysOnTop = panel->aboveWindows();
    request.noActivate = !panel->focusable();
    entry.appBar->create(request);
  } catch (const std::exception& e) {
    qWarning().noquote() << "wabar: the native look of a window failed:" << e.what();
  } catch (...) {
    qWarning().noquote() << "wabar: the native look of a window failed";
  }
}

void ShellHost::decorate(QObject* window, int backdrop) {
  auto* target = qobject_cast<QWindow*>(window);
  if (!target || !options_.hooks.applyBackdrop) return;
  const auto hwnd = static_cast<quintptr>(target->winId());
  options_.hooks.applyBackdrop(hwnd, static_cast<Backdrop::Kind>(backdrop));
}

void ShellHost::watch() {
  if (options_.safe) return;
  watching_ = true;
  refreshWatches();
}

void ShellHost::refreshWatches() {
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

}  // namespace wabar::host

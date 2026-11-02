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
#include <QMutex>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QUrl>
#include <QWindow>
#include <algorithm>
#include <cmath>

#include "host/plugin_guard.h"
#include "host/qml/panel_window.h"
#include "host/qml/qs_window.h"
#include "host/qml/shell_screen.h"
#include "host/startup_trace.h"

namespace waku::host {

namespace {

constexpr auto kFallbackUrl = "qrc:/qt/qml/waku/FallbackShell.qml";
constexpr auto kSafeUrl = "qrc:/qt/qml/waku/widgets/SafeShell.qml";
constexpr auto kDefaultUrl = "qrc:/qt/qml/waku/widgets/DefaultShell.qml";
// Loaded once so the waku types are registered before the host asks for its singletons.
constexpr auto kChromeUrl = "qrc:/qt/qml/waku/WindowChrome.qml";
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

// Keeps the first warning logged while it runs (and passes every message on), so a shell that
// builds nothing can say why. Qt's handler is process-wide: start/stop bracket one build on the
// GUI thread, and the destructor always restores the previous handler.
class WarningCapture {
 public:
  WarningCapture() = default;
  WarningCapture(const WarningCapture&) = delete;
  WarningCapture& operator=(const WarningCapture&) = delete;
  ~WarningCapture() { stop(); }

  void start() {
    if (active_) return;
    QMutexLocker lock(&mutex());
    owner() = this;
    previous() = qInstallMessageHandler(&WarningCapture::handle);
    active_ = true;
  }
  void stop() {
    if (!active_) return;
    QMutexLocker lock(&mutex());
    qInstallMessageHandler(previous());
    owner() = nullptr;
    active_ = false;
  }
  QString first() const {
    QMutexLocker lock(&mutex());
    return first_;
  }

 private:
  static QMutex& mutex() {
    static QMutex m;
    return m;
  }
  static WarningCapture*& owner() {
    static WarningCapture* o = nullptr;
    return o;
  }
  static QtMessageHandler& previous() {
    static QtMessageHandler p = nullptr;
    return p;
  }
  static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QtMessageHandler next = nullptr;
    {
      QMutexLocker lock(&mutex());
      next = previous();
      if (type == QtWarningMsg && owner() && owner()->first_.isEmpty()) {
        owner()->first_ = message.section(QLatin1Char('\n'), 0, 0).trimmed();
      }
    }
    if (next) next(type, context, message);
  }

  bool active_ = false;
  QString first_;
};

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
// clearComponentCache() does not drop it. resetEngine() deletes the windows of the old shell
// before the clear, but anything else made from the old files that is still alive (a JS value
// kept by a singleton set from outside) would make the new shell use the old files. A changing
// query on the URL makes each reload's files new to the engine. The interceptor only touches QML
// files under the config home.
class ShellHost::GenerationInterceptor : public QQmlAbstractUrlInterceptor {
 public:
  explicit GenerationInterceptor(QString root) : root_(std::move(root)) {}
  void nextGeneration() { ++generation_; }

  QUrl intercept(const QUrl& url, DataType type) override {
    if (generation_ == 0 || type != QmlFile || !url.isLocalFile()) return url;
    if (!url.toLocalFile().startsWith(root_, Qt::CaseInsensitive)) return url;
    QUrl versioned = url;
    versioned.setQuery(QStringLiteral("waku=%1").arg(generation_));
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
    // Registers the waku types (static plugin) so singletonInstance() finds Theme and ScreenList.
    QQmlComponent chrome(engine_, QUrl(QString::fromLatin1(kChromeUrl)));
    if (chrome.isError()) {
      qCritical().noquote() << "waku: cannot load the window chrome:"
                            << describeAll(chrome.errors()).join('\n');
    }
  }
  debounce_.setSingleShot(true);
  debounce_.setInterval(150);
  connect(&debounce_, &QTimer::timeout, this, [this] {
    snapshot_ = scanQmlFiles();  // before the reload: an edit during it is seen as a change
    reload();
    refreshWatches();
  });
  trimTimer_.setSingleShot(true);
  connect(&trimTimer_, &QTimer::timeout, this, [this] { trimIfSettled(); });
  watchScreens();
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { configHomeChanged(); });
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
          [this](const QString& file) { configHomeChanged(file); });
}

ShellHost::~ShellHost() {
  // Windows (and their app bars) go before the root object and component they refer to, and the
  // windows deleted later go now, while the engine still exists.
  dropShell();
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
    if (entry.live() && entry.window->backingWindow() == window) {
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
      if (entry.presented && entry.live() && entry.window->backingWindowVisible()) {
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
    qWarning().noquote() << "waku: config errors:\n" << lines.join('\n');
    *error = lines.value(0);
    return false;
  }
  const QMetaObject* root = rootType(component);
  if (!root || !root->inherits(&expected)) {
    *error = QStringLiteral("%1 must declare a %2 at its root")
                 .arg(QFileInfo(url.toLocalFile()).fileName(),
                      QString::fromLatin1(expected.className()).section(QStringLiteral("::"), -1));
    qWarning().noquote() << "waku:" << *error;
    return false;
  }
  if (create) {
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
      const QStringList lines = describeAll(component.errors());
      qWarning().noquote() << "waku: config errors:\n" << lines.join('\n');
      *error = lines.value(0, QStringLiteral("the file could not be created"));
      return false;
    }
  }
  traceStep(create ? "theme checked in the probe engine" : "shell checked in the probe engine");
  return true;
}

bool ShellHost::validateTheme(QString* error) {
  if (!QFileInfo::exists(themePath())) return true;
  return validate(QUrl::fromLocalFile(themePath()), ThemeSpec::staticMetaObject, true, error);
}

void ShellHost::applyTheme(bool defaultsOnly) {
  auto* theme = engine_->singletonInstance<Theme*>(QStringLiteral("waku"), QStringLiteral("Theme"));
  if (!theme) return;
  const ThemeSpec defaults;
  if (options_.safe || defaultsOnly || !QFileInfo::exists(themePath())) {
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

QUrl ShellHost::defaultShellUrl() { return QUrl(QString::fromLatin1(kDefaultUrl)); }

bool ShellHost::checkShell(const QUrl& url, QString* error) {
  return validate(url, ShellRoot::staticMetaObject, false, error);
}

bool ShellHost::buildFallback() {
  QString fallbackError;
  if (auto fallback = build(QUrl(QString::fromLatin1(kFallbackUrl)), &fallbackError)) {
    install(std::move(fallback));
    return true;
  }
  qCritical().noquote() << "waku: the fallback shell failed:" << fallbackError;
  return false;
}

QString ShellHost::failures(const Loaded& loaded, int* built) {
  QStringList errors;
  *built = 0;
  for (const Entry& entry : loaded.entries) {
    if (!entry.live()) continue;
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

bool ShellHost::hasWindows(const Loaded& loaded) {
  return std::any_of(loaded.entries.begin(), loaded.entries.end(),
                     [](const Entry& entry) { return entry.live(); });
}

bool ShellHost::reloadSafe() {
  // Nothing in the config home is read: the built-in shell and the default theme, always.
  return reloadBuiltIn(options_.safeUrl.isValid() ? options_.safeUrl : safeShellUrl());
}

bool ShellHost::reloadBuiltIn(const QUrl& url) {
  resetEngine();
  applyTheme(true);
  QString error;
  std::unique_ptr<Loaded> next = build(url, &error);
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

void ShellHost::watchScreens() {
  screenDebounce_.setSingleShot(true);
  // A dock or undock sends screen, primary and DPI changes for one to three seconds; a shorter
  // wait reloads two or three times in a row while Windows still moves the windows.
  screenDebounce_.setInterval(options_.screenSettleMs);
  connect(&screenDebounce_, &QTimer::timeout, this, [this] {
    if (loaded_) reload();
  });
  const auto changed = [this] { screenDebounce_.start(); };
  connect(qGuiApp, &QGuiApplication::screenAdded, this, [this, changed](QScreen* screen) {
    watchScreen(screen);
    changed();
  });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, changed);
  connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, changed);
  for (QScreen* screen : QGuiApplication::screens()) watchScreen(screen);
}

void ShellHost::watchScreen(QScreen* screen) {
  if (!screen) return;
  connect(screen, &QScreen::logicalDotsPerInchChanged, &screenDebounce_,
          qOverload<>(&QTimer::start), Qt::UniqueConnection);
}

bool ShellHost::reload() {
  traceStep("reload started");
  if (options_.safe) return reloadSafe();
  QString error;
  std::unique_ptr<Loaded> next;
  // The reset below destroys the old shell: remember whether it had windows, so a new shell
  // that creates none (a Variants delegate that failed) is an error and not an empty screen.
  const bool hadWindows = loaded_ && hasWindows(*loaded_);
  WarningCapture warnings;
  const QString legacyPath = QDir(options_.configHome).filePath(options_.legacyEntryFile);
  const bool legacyEntry = !options_.legacyEntryFile.isEmpty() && QFileInfo::exists(legacyPath);
  if (options_.implicitConfigHome && !QFileInfo::exists(entryPath()) && !legacyEntry) {
    // Nothing was configured and nothing is there yet: the built-in bar is the product, not an
    // error. The watcher switches to shell.qml when it appears.
    qInfo().noquote() << "waku: no" << options_.entryFile << "in"
                      << QDir::toNativeSeparators(options_.configHome)
                      << "- showing the built-in bar";
    return reloadBuiltIn(options_.defaultUrl.isValid() ? options_.defaultUrl : defaultShellUrl());
  }
  if (!QFileInfo::exists(entryPath())) {
    if (legacyEntry) {
      error = QStringLiteral(
                  "%1 is the waku 1 format: rename it to %2 and its Config to ShellRoot, Bar to "
                  "Panel, BarLayout to PanelLayout (see docs/building.md)")
                  .arg(options_.legacyEntryFile, options_.entryFile);
    } else {
      error = QStringLiteral("%1 not found in %2")
                  .arg(options_.entryFile, QDir::toNativeSeparators(options_.configHome));
    }
    qWarning().noquote() << "waku:" << error;
  } else if (const QList<PluginFailure> refused =
                 [this] {
                   QList<PluginFailure> found =
                       configPluginFailures(options_.configHome, engine_->importPathList());
                   traceStep("config plugins checked");
                   return found;
                 }();
             !refused.isEmpty()) {
    // A qmldir outside the guarded import roots (beside shell.qml, under `import "./lib"`) names
    // a plugin that does not fit this process: loading it could crash the bar.
    error = QStringLiteral("plugin %1 refused: %2")
                .arg(QDir::toNativeSeparators(refused.first().file), refused.first().reason);
    qWarning().noquote() << "waku:" << error;
  } else if ((!loaded_ || validate(QUrl::fromLocalFile(entryPath()), ShellRoot::staticMetaObject,
                                   false, &error)) &&
             validateTheme(&error)) {
    // The check in a throwaway engine protects the shell on the screen. With none (the first
    // start) there is nothing to protect: a shell that does not compile fails in build() with the
    // same message and the fallback shows it, so the probe would only compile the shell twice.
    // The files compile, but creating the shell can still fail. resetEngine() destroys the old
    // shell, all its windows included, before the new one is built: two generations never live
    // at once. If the build fails, reload() shows the fallback shell.
    interceptor_->nextGeneration();
    resetEngine();
    applyTheme();
    warnings.start();
    next = build(QUrl::fromLocalFile(entryPath()), &error);
    warnings.stop();
  }

  if (next) {
    int built = 0;
    const QString failed = failures(*next, &built);
    if (failed.isEmpty() && !hasWindows(*next) && hadWindows) {
      // Nothing failed loudly, yet no window exists and the old shell is already gone.
      error = QStringLiteral("the shell created no windows");
      if (!warnings.first().isEmpty()) error += QStringLiteral(": ") + warnings.first();
      qWarning().noquote() << "waku:" << error;
      next.reset();
    } else if (!failed.isEmpty() && built == 0) {
      // Not one window of the new shell could be built: nothing to show but the error.
      error = failed;
      qWarning().noquote() << "waku: no window could be built:" << error;
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

void ShellHost::resetEngine() {
  // The old shell first, all of it: Qt requires that nothing made from the old files or holding a
  // singleton outlives the clear. Qt drops the singletons the engine owns (deleting them, so their
  // threads and hooks stop) and creates them again on first use; singletons set from outside keep
  // their value.
  dropShell();
  traceStep("old shell destroyed");
  engine_->clearSingletons();
  traceStep("singletons cleared");
  engine_->clearComponentCache();
  emit singletonsCleared();
  traceStep("engine reset");
}

void ShellHost::dropShell() {
  loaded_.reset();
  // A Variants (QQmlDelegateModel) and a LazyLoader release their windows with deleteLater: those
  // windows, their items, bindings and QQuickWindows would otherwise live on next to the new shell
  // and bind against singletons that are about to be deleted. Delete them now. reload() is never
  // entered from an event of one of these windows (Quickshell.reload() is queued; wakuc and the
  // file watcher come through their own slots), so none of them is on the stack.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

std::unique_ptr<ShellHost::Loaded> ShellHost::build(const QUrl& url, QString* error) {
  if (ScreenList* screens = ScreenList::of(engine_)) {
    screens->setScreens(options_.screens(), QGuiApplication::primaryScreen());
  }
  auto loaded = std::make_unique<Loaded>();
  loaded->component = std::make_unique<QQmlComponent>(engine_, url);
  if (loaded->component->isError()) {
    const QStringList lines = describeAll(loaded->component->errors());
    qWarning().noquote() << "waku: config errors:\n" << lines.join('\n');
    *error = lines.value(0);
    return nullptr;
  }
  traceStep("shell compiled");
  const auto notAShell = [&url, error] {
    *error = QStringLiteral("%1 must declare a ShellRoot at its root")
                 .arg(QFileInfo(url.toLocalFile().isEmpty() ? url.fileName() : url.toLocalFile())
                          .fileName());
    qWarning().noquote() << "waku:" << *error;
  };
  // Checked before anything is created, as validate() does: a root that is no ShellRoot starts
  // nothing.
  if (const QMetaObject* type = rootType(*loaded->component);
      !type || !type->inherits(&ShellRoot::staticMetaObject)) {
    notAShell();
    return nullptr;
  }
  // Windows register while the shell is created (QsWindow::componentComplete); install() shows
  // them once the whole shell exists.
  building_ = loaded.get();
  loaded->root.reset(loaded->component->create());
  building_ = nullptr;
  traceStep("shell created");
  if (!loaded->root) {
    const QStringList lines = describeAll(loaded->component->errors());
    qWarning().noquote() << "waku: config errors:\n" << lines.join('\n');
    *error = lines.value(0, QStringLiteral("the shell could not be created"));
    return nullptr;
  }
  loaded->shell = qobject_cast<ShellRoot*>(loaded->root.get());
  if (!loaded->shell) {
    notAShell();
    return nullptr;
  }
  return loaded;
}

void ShellHost::install(std::unique_ptr<Loaded> loaded) {
  // The new windows exist but are hidden; drop the old ones first so two shells never overlap.
  loaded_.reset();
  loaded_ = std::move(loaded);
  // Showing a window runs QML handlers that can create windows (adopt() shows those) or retire
  // them: walk a snapshot and find each entry again instead of holding an iterator.
  QList<QPointer<QsWindow>> windows;
  for (const Entry& entry : loaded_->entries) windows << entry.window;
  for (const QPointer<QsWindow>& window : windows) {
    if (!window || !loaded_) continue;
    for (Entry& entry : loaded_->entries) {
      if (entry.window != window) continue;
      present(entry);
      break;
    }
  }
  if (!loaded_) return;
  traceStep("shell windows presented");
  if (traceEnabled()) {
    for (const Entry& entry : loaded_->entries) {
      if (!entry.live()) continue;
      // Emitted on the render thread; queued to this (GUI) thread.
      connect(
          entry.window->backingWindow(), &QQuickWindow::frameSwapped, this,
          [] { traceStep("first frame of a shell window"); }, Qt::SingleShotConnection);
    }
  }
  // Objects declared next to the windows start work now that the shell is the live one.
  QList<QObject*> parts;
  if (loaded_->shell) collectParts(loaded_->shell, &parts);
  for (QObject* part : parts) {
    if (part->metaObject()->indexOfMethod("activate()") < 0) continue;
    try {
      QMetaObject::invokeMethod(part, "activate");
    } catch (const std::exception& e) {
      qWarning().noquote() << "waku: a shell part failed to start:" << e.what();
    } catch (...) {
      qWarning().noquote() << "waku: a shell part failed to start";
    }
  }
  traceStep("shell parts activated");
  if (options_.hooks.trimWorkingSet) {
    windowsAtInstall_ = static_cast<int>(this->windows().size());
    trimTimer_.start(options_.trimDelayMs);
  }
}

void ShellHost::trimIfSettled() {
  if (!options_.hooks.trimWorkingSet) return;
  // A popup (a window on screen beyond those of the shell) is open: the pages it touches should
  // stay. Look again later.
  if (windows().size() > windowsAtInstall_) {
    trimTimer_.start(options_.trimDelayMs);
    return;
  }
  try {
    options_.hooks.trimWorkingSet();
  } catch (...) {
    qWarning() << "waku: trimming the working set failed";
  }
  traceStep("working set trimmed");
}

void ShellHost::adopt(QsWindow* window) {
  Loaded* target = building_ ? building_ : loaded_.get();
  if (!target) {
    // No shell of this host is being built or shown (a window made outside reload()).
    window->setPresentable(true);
    return;
  }
  Entry entry;
  entry.window = window;
  target->entries.push_back(std::move(entry));
  if (target == loaded_.get()) {
    // Made after install, by a Variants or a LazyLoader: shown at once.
    if (!window->buildError().isEmpty()) setError(window->buildError());
    present(target->entries.back());
  }
}

void ShellHost::retire(QsWindow* window) {
  // Marks and does not erase: the caller may be inside a loop over the entries or hold one.
  for (Loaded* loaded : {building_, loaded_.get()}) {
    if (!loaded) continue;
    for (Entry& entry : loaded->entries) {
      if (entry.window != window) continue;
      entry.retired = true;
      if (entry.appBar) entry.appBar->remove();
      entry.appBar.reset();
      entry.appBarRequest.reset();
      return;
    }
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
    if (!entry.live() || !entry.presented || window->surfaceName().isEmpty()) continue;
    if (name.isEmpty() || window->surfaceName() == name) list << window;
  }
  return list;
}

void ShellHost::noteVisibility(const QString& name, bool visible) { visibility_[name] = visible; }

void ShellHost::windowShown(QsWindow* window, bool activate) {
  const NativeHooks& hooks = options_.hooks;
  // A handler of the show (onFirstShown) may have retired and hidden the window again.
  if (!window->backingWindowVisible()) return;
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
    qWarning().noquote() << "waku: stacking a window failed:" << e.what();
  } catch (...) {
    qWarning().noquote() << "waku: stacking a window failed";
  }
}

void ShellHost::windowStackingChanged(QsWindow* window, bool activate) {
  windowShown(window, activate);
}

void ShellHost::present(Entry& entry) {
  QsWindow* window = entry.window;
  if (!entry.live() || entry.presented || !window->buildError().isEmpty()) return;
  entry.presented = true;
  // Presses on the shell's windows only (eventFilter), not every event of the process. The window
  // drops the filter when it is destroyed, and Qt drops it when the host is.
  window->backingWindow()->installEventFilter(this);
  // A window the user showed or hid before a reload is in the same state after it. The shell is not
  // live yet, so this neither takes the keyboard focus nor is noted again.
  if (const auto it = visibility_.constFind(window->surfaceName());
      !window->surfaceName().isEmpty() && it != visibility_.constEnd()) {
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
      if (candidate.window != raw) continue;
      applyNative(candidate);
      return;
    }
  });
  window->setPresentable(true);
}

void ShellHost::applyNative(Entry& entry) {
  QsWindow* window = entry.window;
  if (!entry.live() || entry.appBar) return;
  QQuickWindow* backing = window->backingWindow();
  const auto hwnd = static_cast<quintptr>(backing->winId());
  const NativeHooks& hooks = options_.hooks;
  try {
    if (hooks.applyBackdrop) hooks.applyBackdrop(hwnd, window->backdrop());
    // A handler of monitorHandleChanged may retire the window (LazyLoader.active = false).
    if (hooks.monitorHandle) window->setMonitorHandle(hooks.monitorHandle(hwnd));
    auto* panel = qobject_cast<PanelWindow*>(window);
    if (!entry.live() || !panel || !hooks.makeAppBar) return;
    if (!entry.layoutConnected) {
      entry.layoutConnected = true;
      // Everything the request is made of: placement, the reserved zone, layer and focus.
      for (auto signal : {&PanelWindow::layoutChanged, &PanelWindow::exclusiveZoneChanged,
                          &PanelWindow::exclusionModeChanged, &PanelWindow::layerChanged,
                          &PanelWindow::keyboardFocusChanged}) {
        connect(panel, signal, this, [this, panel] { panelLayoutChanged(panel); });
      }
    }
    syncAppBar(entry);
  } catch (const std::exception& e) {
    qWarning().noquote() << "waku: the native look of a window failed:" << e.what();
  } catch (...) {
    qWarning().noquote() << "waku: the native look of a window failed";
  }
}

void ShellHost::panelLayoutChanged(PanelWindow* panel) {
  if (!loaded_) return;
  for (Entry& entry : loaded_->entries) {
    if (entry.window != panel || !entry.live() || !entry.layoutConnected) continue;
    try {
      syncAppBar(entry);
    } catch (const std::exception& e) {
      qWarning().noquote() << "waku: updating an app bar failed:" << e.what();
    } catch (...) {
      qWarning().noquote() << "waku: updating an app bar failed";
    }
  }
}

void ShellHost::syncAppBar(Entry& entry) {
  auto* panel = qobject_cast<PanelWindow*>(entry.window.data());
  const NativeHooks& hooks = options_.hooks;
  if (!entry.live() || !panel || !hooks.makeAppBar) return;
  const std::optional<AppBarEdge> edge = panel->appBarEdge();
  if (!edge) {
    // Not docked any more (the anchors changed): give the edge back.
    if (entry.appBar) entry.appBar->remove();
    entry.appBar.reset();
    entry.appBarRequest.reset();
    panel->setAppBarInset(0);
    return;
  }
  const int zone = panel->reservedZone();
  waku::host::AppBarRequest request;
  request.hwnd = static_cast<quintptr>(panel->backingWindow()->winId());
  request.edge = *edge;
  request.thickness =
      zone > 0 ? zone : panelThickness(*edge, QSize(panel->width(), panel->height()));
  request.screen = panel->targetScreen();
  request.reserveSpace = options_.allowReserve && zone > 0;
  request.alwaysOnTop = panel->aboveWindows();
  request.noActivate = !panel->focusable();
  if (entry.appBar && entry.appBarRequest && *entry.appBarRequest == request) return;

  if (!entry.appBar) entry.appBar = hooks.makeAppBar();
  const bool sameBar = entry.appBarRequest && entry.appBarRequest->edge == request.edge &&
                       entry.appBarRequest->reserveSpace == request.reserveSpace &&
                       entry.appBarRequest->screen == request.screen;
  const bool hadBar = entry.appBarRequest.has_value();
  entry.appBarRequest = request;
  int inset = 0;
  if (sameBar) {
    inset = entry.appBar->reposition(request);
  } else {
    // A new bar, or another edge, screen or mode: start over.
    if (hadBar) entry.appBar->remove();
    inset = entry.appBar->create(request);
  }
  const qreal dpr = request.screen ? request.screen->devicePixelRatio() : 1.0;
  panel->setAppBarInset(static_cast<int>(std::lround(inset / (dpr > 0 ? dpr : 1.0))));
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
  // One walk gives the snapshot and the paths to watch.
  QStringList paths;
  snapshot_ = scanQmlFiles(&paths);
  traceStep("config home scanned");
  watchPaths(paths);
  traceStep("config home watched");
}

ShellHost::FileStamps ShellHost::scanQmlFiles(QStringList* paths) const {
  ++configWalks_;
  FileStamps stamps;
  if (!QFileInfo(options_.configHome).isDir()) {
    // An implicit config home may not exist yet. Watch the nearest folder that does, so creating
    // the config home (and shell.qml in it) is seen.
    if (paths) {
      QString parent = QDir::cleanPath(QDir(options_.configHome).absolutePath());
      for (;;) {
        const int cut = parent.lastIndexOf(QLatin1Char('/'));
        if (cut <= 0) break;
        parent.truncate(cut);
        if (QFileInfo(parent).isDir()) {
          *paths = QStringList{parent};
          break;
        }
      }
    }
    return stamps;
  }
  QDir::Filters filters = QDir::Files;
  if (paths) {
    *paths = QStringList{options_.configHome};
    // Folders are listed whatever their name; the name filters apply to files only.
    filters |= QDir::AllDirs | QDir::NoDotAndDotDot;
  }
  QDirIterator it(options_.configHome, {QStringLiteral("*.qml"), QStringLiteral("qmldir")}, filters,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    if (paths) *paths << path;
    const QFileInfo info = it.fileInfo();
    if (!info.isFile()) continue;
    stamps.insert(path,
                  {info.lastModified().toMSecsSinceEpoch(), static_cast<qint64>(info.size())});
  }
  return stamps;
}

void ShellHost::configHomeChanged(const QString& file) {
  // Any write in a watched folder lands here (a shell saving its state, a cache). Only a change
  // to a QML file or a qmldir, including theme.qml, rebuilds the shell.
  ++configChanges_;
  // The config home appeared after watch() ran: watch it now, then compare, so a shell.qml
  // written between the two is not missed.
  if (watching_ && QFileInfo(options_.configHome).isDir()) {
    const QString home = QDir::cleanPath(options_.configHome);
    bool watched = false;
    for (const QString& dir : watcher_.directories()) {
      if (QDir::cleanPath(dir) == home) watched = true;
    }
    if (!watched) refreshWatches();
  }
  if (!file.isEmpty()) {
    // A watched file changed: one stat against the snapshot answers, no walk. A file the snapshot
    // does not know is not a QML file we watch, so the walk decides.
    const auto known = snapshot_.constFind(file);
    if (known != snapshot_.constEnd()) {
      const QFileInfo info(file);
      const bool same = info.isFile() && std::pair<qint64, qint64>{
                                             info.lastModified().toMSecsSinceEpoch(),
                                             static_cast<qint64>(info.size())} == known.value();
      if (!same) debounce_.start();
      return;
    }
  }
  if (scanQmlFiles() != snapshot_) debounce_.start();
}

void ShellHost::refreshWatches() {
  if (!watching_) return;
  QStringList paths;
  scanQmlFiles(&paths);
  watchPaths(paths);
}

void ShellHost::watchPaths(const QStringList& paths) {
  // A saved file is often replaced, so every path is added again after each change.
  const QStringList old = watcher_.files() + watcher_.directories();
  if (!old.isEmpty()) watcher_.removePaths(old);
  watcher_.addPaths(paths);
}

}  // namespace waku::host

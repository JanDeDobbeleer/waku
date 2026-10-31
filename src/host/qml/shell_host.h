#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlComponent>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <utility>

#include "host/app_bar.h"
#include "host/mask_poller.h"
#include "host/qml/bar_theme.h"
#include "host/qml/shell_config.h"
#include "host/qml/window_enums.h"

class QQmlEngine;
class QQuickWindow;
class QScreen;

namespace wabar::host {

class PanelWindow;
class QsWindow;

// Builds the shell declared in <config home>\shell.qml and keeps it up to date.
//
// While a shell is on the screen, reload() first checks shell.qml and theme.qml in a throwaway
// engine: they compile, their types exist and their roots are a ShellRoot and a ThemeSpec. Nothing
// is created there, so no widget is built and no service starts. Only then is the Theme singleton
// filled and the new shell built in the live engine. A file that fails the check leaves the
// previous shell in place: `error` holds the first message (each PanelWindow shows it as a pill)
// and the full list goes to the log. The first load has no shell to protect and skips the shell
// check; build() refuses a root that is no ShellRoot before it creates anything.
//
// A check that passes starts a clean engine, as Quickshell does: the old shell is destroyed, then
// the engine drops its QML singletons (QQmlEngine::clearSingletons) and compiled files, then
// `singletonsCleared` fires and the new shell is built. Singletons survive a plain
// clearComponentCache() with their old types, and the new shell then mixes old and new types
// (`Unable to assign Foo to Foo`). Nothing may keep a pointer to a singleton across a reload:
// resolve it again after `singletonsCleared`. A shell that then cannot be created shows the
// fallback with the error, because the old one is gone.
//
// The windows of a shell (PanelWindow, FloatingWindow, wherever they are declared) register
// themselves with the host of their engine (the context property `ShellHost`). A window that fails
// to build is skipped and its error recorded; the others still show (proposal 4.1.3). If the very
// first load fails, or no window of the new shell could be built, there is nothing to keep, so a
// built-in fallback shell shows the error. A config home that still has the 1.x `bar.qml` and no
// `shell.qml` gets an error naming the rename.
//
// Threading: GUI thread only. Owns the loaded shell; its windows go first, then the root object,
// then its component, when it is replaced or the host is destroyed. watch() reloads after a
// change in the config home, debounced.
class ShellHost : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)

 public:
  // The parts that touch the real desktop, replaceable so tests never register an app bar or
  // blur a window.
  struct NativeHooks {
    // One per app bar window; null function: no app bar object at all.
    std::function<std::unique_ptr<wabar::host::IAppBar>()> makeAppBar;
    std::function<void(quintptr hwnd, Backdrop::Kind backdrop)> applyBackdrop;
    // HMONITOR of the monitor the window is on.
    std::function<qlonglong(quintptr hwnd)> monitorHandle;
    // The cursor in physical screen pixels, for window masks (MaskPoller). Null: masks are inert.
    // win32() sets it only on a live desktop.
    CursorSource cursorPosition;
    // Keeps the window above the others (SetWindowPos HWND_TOPMOST, no activation): called each
    // time a window of the Top or Overlay layer is shown.
    std::function<void(quintptr hwnd)> raiseTopmost;
    // Sets or clears WS_EX_NOACTIVATE: clicks on a window with `keyboardFocus: None` never take
    // the focus. Called each time a window is shown or its layer or keyboard focus changes.
    std::function<void(quintptr hwnd, bool noActivate)> setNoActivate;
    // Takes the keyboard focus for the window although another process has the foreground
    // (kernel::forceForegroundFocus). Called when a window with keyboard focus is shown at run
    // time.
    std::function<void(quintptr hwnd)> forceForeground;

    static NativeHooks win32();
  };

  struct Options {
    QString configHome;
    QString entryFile = QStringLiteral("shell.qml");
    // The 1.x entry file: when it is there and `entryFile` is not, the error says to rename it.
    QString legacyEntryFile = QStringLiteral("bar.qml");
    // Optional: a ThemeSpec that sets the look; missing means the defaults.
    QString themeFile = QStringLiteral("theme.qml");
    // Reserving the work area changes the user's desktop, so it needs this on top of a window's
    // own exclusive zone (the `--register-app-bar` switch).
    bool allowReserve = false;
    NativeHooks hooks;
    // Screens to build shells for (ScreenList); default: every screen of the application.
    std::function<QList<QScreen*>()> screens;
    // Safe mode (`--safe`): the built-in shell and the default theme; the config home is ignored,
    // not read and not watched. For getting a bar back when the config is broken.
    bool safe = false;
    // The shell safe mode builds; default: the built-in one (safeShellUrl()).
    QUrl safeUrl;
  };

  // The built-in shell of safe mode: Clock, Workspaces, Volume, Battery, Cpu and Memory.
  static QUrl safeShellUrl();
  // The host of `engine`, or null.
  static ShellHost* of(QQmlEngine* engine);

  ShellHost(QQmlEngine* engine, Options options, QObject* parent = nullptr);
  ~ShellHost() override;

  // True when the shell was applied (some windows may still have failed: see `error`). False
  // leaves the previous shell in place when the files failed the check, and shows the fallback
  // when the new shell could not be created or there was no shell.
  bool reload();
  // Reloads whenever a file in the config home changes. Does nothing in safe mode.
  void watch();

  // Whether `url` is a shell that would load: it compiles and its root is a ShellRoot. Creates
  // nothing. `error` gets the first message when it does not.
  bool checkShell(const QUrl& url, QString* error);

  // Gives a window the native look of a shell window (the backdrop). PopupWindow and Tooltip call
  // this for their own windows with the backdrop of the window they belong to. `window` is a
  // QWindow; `backdrop` is a Backdrop value.
  Q_INVOKABLE void decorate(QObject* window, int backdrop);

  // A shell window does not take focus, so a popup cannot rely on losing activation to know the
  // user clicked elsewhere in the bar: the host reports those presses (windowPressed).
  bool eventFilter(QObject* watched, QEvent* event) override;

  const NativeHooks& hooks() const { return options_.hooks; }
  // The one timer behind every window mask; null when there is no cursor source.
  MaskPollerHub* maskHub() const { return maskHub_.get(); }
  QString error() const { return error_; }
  // The backing windows on screen, in the order they registered.
  QList<QQuickWindow*> windows() const;
  ShellRoot* shell() const;

  // The windows with a `name` (`name` empty: all of them), in the order they registered. The
  // surfaces `wabarc surface ...` addresses.
  QList<QsWindow*> namedWindows(const QString& name = {}) const;

  // ---- called by QsWindow ----
  void adopt(QsWindow* window);
  // The window is dying later (deleteLater): it gives back its app bar and stops counting as a
  // window of the shell now. Its entry goes with release().
  void retire(QsWindow* window);
  void release(QsWindow* window);
  // A window just appeared on the screen: stack it, set whether it takes clicks as activation and,
  // with `activate`, give it the keyboard focus.
  void windowShown(QsWindow* window, bool activate);
  // The layer or the keyboard focus of a window on the screen changed.
  void windowStackingChanged(QsWindow* window);
  // A named window was shown or hidden at run time; kept across reloads (see `present`).
  void noteVisibility(const QString& name, bool visible);

 signals:
  void errorChanged();
  // A mouse button went down in one of the shell windows.
  void windowPressed();
  void reloaded(bool ok);
  // The engine dropped its QML singletons and is about to build a shell: whoever holds or wires a
  // singleton does it again now.
  void singletonsCleared();

 private:
  struct Entry {
    QPointer<QsWindow> window;
    std::unique_ptr<wabar::host::IAppBar> appBar;
    // What the app bar was last asked for; a panel's later layout is compared with it.
    std::optional<wabar::host::AppBarRequest> appBarRequest;
    bool layoutConnected = false;
    bool presented = false;
    // Set by retire(): the window waits for its deferred deletion and is skipped everywhere.
    bool retired = false;
    bool live() const { return window && !retired; }
  };
  // Members are destroyed in reverse order: windows first, then the root, then its component.
  struct Loaded {
    ~Loaded();
    std::unique_ptr<QQmlComponent> component;
    std::unique_ptr<QObject> root;
    ShellRoot* shell = nullptr;
    // A list: entries are added while windows build and must not move.
    std::list<Entry> entries;
  };

  // Compiles `url` in a throwaway engine and checks that its root is an `expected`. With
  // `create`, the root is also created (theme.qml: a ThemeSpec has no side effects).
  bool validate(const QUrl& url, const QMetaObject& expected, bool create, QString* error);
  // theme.qml, when there is one, passes the same check (as a ThemeSpec).
  bool validateTheme(QString* error);
  // Copies theme.qml (or the defaults, when there is none) into the Theme singleton.
  void applyTheme();
  bool reloadSafe();
  // Reloads when the screens change, so a docked monitor gets its bar and a DPI change its layout.
  void watchScreens();
  void watchScreen(QScreen* screen);
  // Destroys the shell, drops the engine's singletons and compiled files, emits singletonsCleared.
  void resetEngine();
  // Destroys the shell and every window of it that waits for a deferred deletion.
  void dropShell();
  std::unique_ptr<Loaded> build(const QUrl& url, QString* error);
  bool buildFallback();
  void install(std::unique_ptr<Loaded> loaded);
  void present(Entry& entry);
  void applyNative(Entry& entry);
  // Creates, renegotiates or removes the panel's app bar to match its layout now, and tells the
  // panel how far the shell pushed it in.
  void syncAppBar(Entry& entry);
  void panelLayoutChanged(PanelWindow* panel);
  void setError(const QString& error);
  void refreshWatches();
  // Path -> (mtime, size) of every .qml file and qmldir under the config home. A change event
  // reloads only when this differs from `snapshot_`, so a state file or a cache does not.
  using FileStamps = QHash<QString, std::pair<qint64, qint64>>;
  FileStamps scanQmlFiles() const;
  void configHomeChanged();
  QString entryPath() const;
  QString themePath() const;
  // The windows of `loaded` that failed to build, as "N windows failed: <first error>".
  static QString failures(const Loaded& loaded, int* built);
  // Whether `loaded` has a window that is not retired.
  static bool hasWindows(const Loaded& loaded);

  // Gives the QML files of the config home a new URL for each reload (see shell_host.cpp), so a
  // reload never reuses a file that something of the old shell still holds.
  class GenerationInterceptor;

  QQmlEngine* engine_;
  Options options_;
  std::unique_ptr<GenerationInterceptor> interceptor_;
  std::unique_ptr<MaskPollerHub> maskHub_;
  std::unique_ptr<Loaded> loaded_;
  // The shell being built: windows that complete now are added here, shown by install().
  Loaded* building_ = nullptr;
  QString error_;
  // name -> shown, for named windows toggled at run time: a reload gives the new windows of that
  // name the same state, so an open launcher stays open while the user edits the config.
  QHash<QString, bool> visibility_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  // A screen added or removed, a new primary screen or a DPI change: one reload after they settle.
  QTimer screenDebounce_;
  FileStamps snapshot_;
  bool watching_ = false;
};

}  // namespace wabar::host

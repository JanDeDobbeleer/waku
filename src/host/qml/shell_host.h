#pragma once

#include <QFileSystemWatcher>
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

#include "host/app_bar.h"
#include "host/qml/bar_theme.h"
#include "host/qml/shell_config.h"
#include "host/qml/window_enums.h"

class QQmlEngine;
class QQuickWindow;
class QScreen;

namespace wabar::host {

class QsWindow;

// Builds the shell declared in <config home>\shell.qml and keeps it up to date.
//
// reload() first checks shell.qml and theme.qml in a throwaway engine: they compile, their types
// exist and their roots are a ShellRoot and a ThemeSpec. Nothing is created there, so no widget is
// built and no service starts. Only then is the Theme singleton filled and the new shell built in
// the live engine, next to the old one: its windows stay hidden until it is complete, and the old
// shell goes only when the new one is installed. A file that fails the check, a shell that cannot
// be created, or one with no window where the old one had some leaves the previous shell in place:
// `error` holds the first message (each PanelWindow shows it as a pill) and the full list goes to
// the log.
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
  // leaves the previous shell in place, or shows the fallback when there is none.
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

  QString error() const { return error_; }
  // The backing windows on screen, in the order they registered.
  QList<QQuickWindow*> windows() const;
  ShellRoot* shell() const;

  // ---- called by QsWindow ----
  void adopt(QsWindow* window);
  void release(QsWindow* window);

 signals:
  void errorChanged();
  // A mouse button went down in one of the shell windows.
  void windowPressed();
  void reloaded(bool ok);

 private:
  struct Entry {
    QPointer<QsWindow> window;
    std::unique_ptr<wabar::host::IAppBar> appBar;
    bool presented = false;
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
  std::unique_ptr<Loaded> build(const QUrl& url, QString* error);
  bool buildFallback();
  void install(std::unique_ptr<Loaded> loaded);
  void present(Entry& entry);
  void applyNative(Entry& entry);
  void setError(const QString& error);
  void refreshWatches();
  QString entryPath() const;
  QString themePath() const;
  // The windows of `loaded` that failed to build, as "N windows failed: <first error>".
  static QString failures(const Loaded& loaded, int* built);

  // Gives the QML files of the config home a new URL for each reload (see shell_host.cpp), so a
  // reload built next to the live shell never reuses a file the live shell still holds.
  class GenerationInterceptor;

  QQmlEngine* engine_;
  Options options_;
  std::unique_ptr<GenerationInterceptor> interceptor_;
  std::unique_ptr<Loaded> loaded_;
  // The shell being built: windows that complete now are added here, shown by install().
  Loaded* building_ = nullptr;
  QString error_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  bool watching_ = false;
};

}  // namespace wabar::host

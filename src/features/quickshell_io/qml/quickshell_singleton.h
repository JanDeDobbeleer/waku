#pragma once

#include <QtQml/qqmlregistration.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>

#include "features/quickshell_io/command_spec.h"
#include "features/quickshell_io/qml/clipboard_text.h"
#include "kernel/qt/sealed_singleton.h"

namespace waku::quickshell_io {

// The window-free members of Quickshell's `Quickshell` singleton: env, execDetached, the path
// helpers, clipboardText, processId, iconPath, reload and the version checks. Registered as
// `QuickshellSingleton` in waku.services (and so in Quickshell.Io through its import).
//
// The QML name `Quickshell` is src/quickshell/core/qml/Quickshell.qml: it adds `screens` and
// forwards the rest to this class. The composition root (src/app/waku/quickshell_glue.h) connects
// reloadRequested(bool) to the shell host's reload, calls setShellDir() with the config home and
// emits reloadCompleted() / reloadFailed().
//
// GUI thread only. Widgets are trusted code: execDetached starts any program.
class QuickshellSingleton : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(QuickshellSingleton)
  QML_SINGLETON
  QML_EXTENDED(waku::kernel::SealedSingleton)
  Q_PROPERTY(qint64 processId READ processId CONSTANT FINAL)
  Q_PROPERTY(QString clipboardText READ clipboardText WRITE setClipboardText NOTIFY
                 clipboardTextChanged FINAL)
  Q_PROPERTY(QString shellDir READ shellDir NOTIFY shellDirChanged FINAL)
  Q_PROPERTY(QString shellRoot READ shellDir NOTIFY shellDirChanged FINAL)
  Q_PROPERTY(QString configDir READ shellDir NOTIFY shellDirChanged FINAL)
  Q_PROPERTY(QString dataDir READ dataDir CONSTANT FINAL)
  Q_PROPERTY(QString stateDir READ stateDir CONSTANT FINAL)
  Q_PROPERTY(QString cacheDir READ cacheDir CONSTANT FINAL)
  Q_PROPERTY(bool watchFiles READ watchFiles WRITE setWatchFiles NOTIFY watchFilesChanged FINAL)
  Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY
                 workingDirectoryChanged FINAL)

 public:
  struct Dirs {
    QString shell;
    QString data;
    QString state;
    QString cache;
  };
  // The real directories: shell = $WAKU_CONFIG_HOME or <home>\.config\waku; data, state and
  // cache = %LOCALAPPDATA%\waku\{data,state,cache}. Nothing is created.
  static Dirs defaultDirs();

  using Launcher = std::function<bool(const CommandSpec&)>;

  explicit QuickshellSingleton(QObject* parent = nullptr);
  // Tests give fake folders and a launcher that starts nothing.
  QuickshellSingleton(Dirs dirs, Launcher launcher, QObject* parent = nullptr);

  qint64 processId() const;
  // Read on first use and cached until the clipboard changes (see ClipboardText).
  QString clipboardText() const { return clipboard_.text(); }
  void setClipboardText(const QString& text) { clipboard_.setText(text); }
  QString shellDir() const { return dirs_.shell; }
  // The host sets the config home it loaded; the first reading may come before that.
  void setShellDir(const QString& dir);
  QString dataDir() const { return dirs_.data; }
  QString stateDir() const { return dirs_.state; }
  QString cacheDir() const { return dirs_.cache; }
  bool watchFiles() const { return watchFiles_; }
  void setWatchFiles(bool watch);
  QString workingDirectory() const;
  void setWorkingDirectory(const QString& directory);

  // The value of an environment variable, null when it is not set.
  Q_INVOKABLE QVariant env(const QString& name) const;
  // Starts a command (a list, or an object with command, environment, clearEnvironment,
  // workingDirectory) detached and hidden.
  Q_INVOKABLE void execDetached(const QVariant& context);
  Q_INVOKABLE QString shellPath(const QString& path) const;
  Q_INVOKABLE QString configPath(const QString& path) const { return shellPath(path); }
  Q_INVOKABLE QString dataPath(const QString& path) const;
  Q_INVOKABLE QString statePath(const QString& path) const;
  Q_INVOKABLE QString cachePath(const QString& path) const;
  // STUB: waku has no icon-theme provider yet (TODO: an image://icon/ provider over the Start
  // menu and shell icons). Returns an image://icon/<name> URL when the Qt theme has the icon, a
  // file path or URL as is, "" when `checkOrFallback` is true and the icon is unknown, the fallback
  // when it is a string, and the name otherwise.
  Q_INVOKABLE QString iconPath(const QString& icon, const QVariant& checkOrFallback = {}) const;
  Q_INVOKABLE bool hasThemeIcon(const QString& icon) const;
  // Quickshell version 0.3 is emulated.
  Q_INVOKABLE bool hasVersion(int major, int minor, const QVariant& features = {}) const;
  Q_INVOKABLE bool hasQtVersion(int major, int minor) const;
  // Asks the host to reload the config. `hard` also drops caches (the host decides what).
  Q_INVOKABLE void reload(bool hard = false);
  Q_INVOKABLE void inhibitReloadPopup() {}

 signals:
  void clipboardTextChanged();
  void shellDirChanged();
  void watchFilesChanged();
  void workingDirectoryChanged();
  void reloadRequested(bool hard);
  void reloadCompleted();
  void reloadFailed(const QString& errorString);
  void lastWindowClosed();

 private:
  static QString join(const QString& base, const QString& path);

  Dirs dirs_;
  Launcher launcher_;
  ClipboardText clipboard_;
  bool watchFiles_ = true;
};

// Quickshell's ElapsedTimer: a stopwatch. elapsed() and restart() return seconds.
class ElapsedTimer : public QObject {
  Q_OBJECT
  QML_ELEMENT

 public:
  explicit ElapsedTimer(QObject* parent = nullptr);

  Q_INVOKABLE double elapsed() const;
  Q_INVOKABLE double elapsedMs() const;
  Q_INVOKABLE double elapsedNs() const;
  Q_INVOKABLE double restart();
  Q_INVOKABLE double restartMs();
  Q_INVOKABLE double restartNs();

 private:
  QElapsedTimer timer_;
};

}  // namespace waku::quickshell_io

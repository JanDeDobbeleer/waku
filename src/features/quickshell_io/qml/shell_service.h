#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace wabar::quickshell_io {

// What a widget does on click, as a QML singleton: start a program, run a command line, open a
// URL, file or folder. Nothing is returned from a launch beyond whether it started; use
// ShellProcess for a command whose output a widget shows. Everything runs without a console window
// and never blocks the GUI thread (open() runs ShellExecute on a worker thread). GUI thread only.
// Widgets are trusted code: this starts any program with the bar's privileges.
class Shell : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QString homeDir READ homeDir CONSTANT FINAL)
  Q_PROPERTY(QString configHome READ configHome CONSTANT FINAL)

 public:
  // The three launches, replaceable so tests never start a real program.
  struct Launcher {
    std::function<bool(const QString& program, const QStringList& arguments)> start;
    std::function<bool(const QString& commandLine)> command;
    std::function<void(const QString& target)> open;
  };

  // Real launches; the config home is $WABAR_CONFIG_HOME or <home>\.config\wabar.
  explicit Shell(QObject* parent = nullptr);
  Shell(Launcher launcher, QString configHome, QObject* parent);

  QString homeDir() const;
  QString configHome() const { return configHome_; }

  // Starts `program` detached. False when `program` is empty or cannot start.
  Q_INVOKABLE bool start(const QString& program, const QStringList& arguments = {});
  // Runs `cmd /c <commandLine>` hidden and detached. False when empty or it cannot start.
  Q_INVOKABLE bool command(const QString& commandLine);
  // ShellExecute "open" on a URL, file or folder. An empty target is ignored.
  Q_INVOKABLE void open(const QString& target);
  // The value of an environment variable, "" when it is not set.
  Q_INVOKABLE QString env(const QString& name) const;

 private:
  Launcher launcher_;
  QString configHome_;
};

}  // namespace wabar::quickshell_io

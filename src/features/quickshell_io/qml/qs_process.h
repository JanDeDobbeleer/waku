#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <memory>

#include "features/quickshell_io/process_job.h"
#include "features/quickshell_io/qml/data_stream_parsers.h"

// <cstdio> defines stdout and stderr as macros, which moc cannot parse in the properties below.
// Nothing in this unit uses the C streams.
#undef stdout
#undef stderr

namespace wabar::quickshell_io {

// Quickshell.Io Process: a tracked child process. Setting `running` to true starts `command` (no
// shell, no console window; before the command is set it starts when the command arrives); false
// kills the process and its descendants. A killed process reports the signal as Quickshell does on
// Linux: exited(15, CrashExit) for `running = false`, exited(n, CrashExit) for signal(n). `stdout`
// and `stderr` take a StdioCollector or a SplitParser; without one the stream is discarded. The
// child and its tree are killed when the object is destroyed. Windows has no signals: signal()
// kills for the terminating numbers (2, 3, 9, 15) and ignores the rest. GUI thread only; QProcess
// does the I/O asynchronously. Widgets are trusted code: this starts any program with the bar's
// privileges.
class QsProcess : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Process)
  Q_PROPERTY(QStringList command READ command WRITE setCommand NOTIFY commandChanged FINAL)
  Q_PROPERTY(
      QVariantMap environment READ environment WRITE setEnvironment NOTIFY environmentChanged FINAL)
  Q_PROPERTY(bool clearEnvironment READ clearEnvironment WRITE setClearEnvironment NOTIFY
                 clearEnvironmentChanged FINAL)
  Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY
                 workingDirectoryChanged FINAL)
  Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged FINAL)
  Q_PROPERTY(
      bool stdinEnabled READ stdinEnabled WRITE setStdinEnabled NOTIFY stdinEnabledChanged FINAL)
  Q_PROPERTY(QVariant processId READ processId NOTIFY processIdChanged FINAL)
  Q_PROPERTY(QVariant pid READ processId NOTIFY processIdChanged FINAL)
  Q_PROPERTY(wabar::quickshell_io::DataStreamParser* stdout READ stdoutParser WRITE setStdoutParser
                 NOTIFY stdoutChanged FINAL)
  Q_PROPERTY(wabar::quickshell_io::DataStreamParser* stderr READ stderrParser WRITE setStderrParser
                 NOTIFY stderrChanged FINAL)

 public:
  enum ExitStatus { NormalExit = QProcess::NormalExit, CrashExit = QProcess::CrashExit };
  Q_ENUM(ExitStatus)

  explicit QsProcess(QObject* parent = nullptr);
  ~QsProcess() override;

  QStringList command() const { return command_; }
  void setCommand(const QStringList& command);
  QVariantMap environment() const { return environment_; }
  void setEnvironment(const QVariantMap& environment);
  bool clearEnvironment() const { return clearEnvironment_; }
  void setClearEnvironment(bool clear);
  QString workingDirectory() const { return workingDirectory_; }
  void setWorkingDirectory(const QString& directory);
  bool running() const { return process_ != nullptr; }
  void setRunning(bool running);
  bool stdinEnabled() const { return stdinEnabled_; }
  void setStdinEnabled(bool enabled);
  QVariant processId() const;
  DataStreamParser* stdoutParser() const { return stdout_; }
  void setStdoutParser(DataStreamParser* parser);
  DataStreamParser* stderrParser() const { return stderr_; }
  void setStderrParser(DataStreamParser* parser);

  // Stops any running process, applies `context` (a list: the command; or an object with command,
  // environment, clearEnvironment, workingDirectory) and starts again.
  Q_INVOKABLE void exec(const QVariant& context);
  // Starts the command untracked: `running` stays false and the child outlives this object.
  Q_INVOKABLE void startDetached();
  Q_INVOKABLE void signal(int signal);
  // Writes to stdin; does nothing unless the process runs with stdinEnabled.
  Q_INVOKABLE void write(const QString& data);

 signals:
  void commandChanged();
  void environmentChanged();
  void clearEnvironmentChanged();
  void workingDirectoryChanged();
  void runningChanged();
  void stdinEnabledChanged();
  void processIdChanged();
  void stdoutChanged();
  void stderrChanged();
  void started();
  void exited(int exitCode, wabar::quickshell_io::QsProcess::ExitStatus exitStatus);

 private:
  // Starts the process when `running` was asked for, none runs and there is a command.
  void startIfReady();
  void start();
  void kill(int signal);
  void drain();
  // Ends the current run: reads what is left, closes the streams and reports `exited`.
  void finishRun(int exitCode, ExitStatus status);
  void onFinished(int exitCode, QProcess::ExitStatus status);

  QStringList command_;
  QVariantMap environment_;
  bool clearEnvironment_ = false;
  QString workingDirectory_;
  bool stdinEnabled_ = false;
  QPointer<DataStreamParser> stdout_;
  QPointer<DataStreamParser> stderr_;
  QProcess* process_ = nullptr;
  std::unique_ptr<ProcessJob> job_;
  qint64 pid_ = 0;
  // `running = true` was set and has not been spent: the next start (a command may still be
  // missing) goes ahead.
  bool wantRunning_ = false;
  // The signal that killed the current run, 0 while nobody asked.
  int killSignal_ = 0;
};

}  // namespace wabar::quickshell_io

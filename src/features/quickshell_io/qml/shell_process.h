#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQmlParserStatus>
#include <QStringList>
#include <QTimer>
#include <memory>

#include "features/quickshell_io/process_job.h"

namespace wabar::quickshell_io {

// Runs a command without a console window and exposes its output to QML. `run()` starts it once;
// `interval > 0` also reruns it every interval ms (a run still in progress is not restarted).
// A run is killed after `timeout` ms, with its descendants (a ProcessJob per run, as QsProcess
// does). Lives on the GUI thread; QProcess does the I/O asynchronously. Stopped by destroying the
// object (a running child and its descendants are killed).
// Widgets are trusted code: a user widget can run any program with the bar's privileges.
class ShellProcess : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_NAMED_ELEMENT(ShellProcess)
  Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY programChanged FINAL)
  Q_PROPERTY(QStringList arguments READ arguments WRITE setArguments NOTIFY argumentsChanged FINAL)
  Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged FINAL)
  Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged FINAL)
  Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
  Q_PROPERTY(QString output READ output NOTIFY finished FINAL)
  Q_PROPERTY(QString errorOutput READ errorOutput NOTIFY finished FINAL)
  Q_PROPERTY(int exitCode READ exitCode NOTIFY finished FINAL)

 public:
  explicit ShellProcess(QObject* parent = nullptr);
  ~ShellProcess() override;

  QString program() const { return program_; }
  void setProgram(const QString& program);
  QStringList arguments() const { return arguments_; }
  void setArguments(const QStringList& arguments);
  int interval() const { return interval_; }
  void setInterval(int ms);
  int timeout() const { return timeout_; }
  void setTimeout(int ms);
  bool running() const { return running_; }
  QString output() const { return output_; }
  QString errorOutput() const { return errorOutput_; }
  int exitCode() const { return exitCode_; }
  // Whether the `interval` timer will run the command again (tests).
  bool rerunScheduled() const { return intervalTimer_.isActive(); }

  Q_INVOKABLE void run();

  void classBegin() override {}
  void componentComplete() override;

 signals:
  void programChanged();
  void argumentsChanged();
  void intervalChanged();
  void timeoutChanged();
  void runningChanged();
  void finished();

 private:
  void restartTimer();
  void finish(int exitCode);
  void setRunning(bool running);
  // Kills the running child and, when the job took it, every descendant.
  void killTree();

  QString program_;
  QStringList arguments_;
  int interval_ = 0;
  int timeout_ = 10000;
  bool running_ = false;
  bool complete_ = false;
  QString output_;
  QString errorOutput_;
  int exitCode_ = -1;
  QProcess* process_ = nullptr;
  std::unique_ptr<ProcessJob> job_;
  bool killed_ = false;  // this run was killed (timeout or destruction)
  QTimer intervalTimer_;
  QTimer timeoutTimer_;
};

}  // namespace wabar::quickshell_io

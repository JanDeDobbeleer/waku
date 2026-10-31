#include "features/quickshell_io/qml/shell_process.h"

#include <algorithm>

#include "kernel/win32/shell_launch.h"

namespace wabar::quickshell_io {

ShellProcess::ShellProcess(QObject* parent) : QObject(parent) {
  connect(&intervalTimer_, &QTimer::timeout, this, [this] { run(); });
  timeoutTimer_.setSingleShot(true);
  connect(&timeoutTimer_, &QTimer::timeout, this, [this] {
    if (process_) process_->kill();
  });
}

ShellProcess::~ShellProcess() {
  if (process_) {
    process_->disconnect(this);
    process_->kill();
    process_->waitForFinished(500);
  }
}

void ShellProcess::setProgram(const QString& program) {
  if (program == program_) return;
  program_ = program;
  emit programChanged();
}

void ShellProcess::setArguments(const QStringList& arguments) {
  if (arguments == arguments_) return;
  arguments_ = arguments;
  emit argumentsChanged();
}

void ShellProcess::setInterval(int ms) {
  ms = std::max(ms, 0);
  if (ms == interval_) return;
  interval_ = ms;
  restartTimer();
  emit intervalChanged();
}

void ShellProcess::setTimeout(int ms) {
  ms = std::max(ms, 100);
  if (ms == timeout_) return;
  timeout_ = ms;
  emit timeoutChanged();
}

void ShellProcess::componentComplete() {
  complete_ = true;
  restartTimer();
  if (interval_ > 0) run();
}

void ShellProcess::restartTimer() {
  if (complete_ && interval_ > 0)
    intervalTimer_.start(interval_);
  else
    intervalTimer_.stop();
}

void ShellProcess::setRunning(bool running) {
  if (running == running_) return;
  running_ = running;
  emit runningChanged();
}

void ShellProcess::run() {
  if (process_ || program_.isEmpty()) return;
  process_ = new QProcess(this);
  kernel::hideConsole(*process_);
  connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    // finished() is not emitted when the program cannot start.
    if (error == QProcess::FailedToStart) finish(-1);
  });
  connect(process_, &QProcess::finished, this,
          [this](int code, QProcess::ExitStatus) { finish(code); });
  setRunning(true);
  timeoutTimer_.start(timeout_);
  process_->start(program_, arguments_);
}

void ShellProcess::finish(int exitCode) {
  if (!process_) return;
  timeoutTimer_.stop();
  output_ = QString::fromUtf8(process_->readAllStandardOutput()).trimmed();
  errorOutput_ = QString::fromUtf8(process_->readAllStandardError()).trimmed();
  exitCode_ = exitCode;
  process_->deleteLater();
  process_ = nullptr;
  setRunning(false);
  emit finished();
}

}  // namespace wabar::quickshell_io

#include "features/quickshell_io/qml/qs_process.h"

#include <QDebug>

#include "features/quickshell_io/command_spec.h"
#include "kernel/win32/shell_launch.h"

namespace wabar::quickshell_io {

QsProcess::QsProcess(QObject* parent) : QObject(parent) {}

QsProcess::~QsProcess() {
  if (!process_) return;
  process_->disconnect(this);
  kill(15);
  process_->waitForFinished(500);
  delete process_;
  process_ = nullptr;
  job_.reset();  // closing the job kills any descendant that is left
}

void QsProcess::setCommand(const QStringList& command) {
  if (command == command_) return;
  command_ = command;
  emit commandChanged();
  startIfReady();
}

void QsProcess::setEnvironment(const QVariantMap& environment) {
  if (environment == environment_) return;
  environment_ = environment;
  emit environmentChanged();
}

void QsProcess::setClearEnvironment(bool clear) {
  if (clear == clearEnvironment_) return;
  clearEnvironment_ = clear;
  emit clearEnvironmentChanged();
}

void QsProcess::setWorkingDirectory(const QString& directory) {
  if (directory == workingDirectory_) return;
  workingDirectory_ = directory;
  emit workingDirectoryChanged();
}

void QsProcess::setStdinEnabled(bool enabled) {
  if (enabled == stdinEnabled_) return;
  stdinEnabled_ = enabled;
  emit stdinEnabledChanged();
}

QVariant QsProcess::processId() const {
  return process_ && pid_ > 0 ? QVariant::fromValue(pid_) : QVariant();
}

void QsProcess::setStdoutParser(DataStreamParser* parser) {
  if (parser == stdout_) return;
  stdout_ = parser;
  emit stdoutChanged();
}

void QsProcess::setStderrParser(DataStreamParser* parser) {
  if (parser == stderr_) return;
  stderr_ = parser;
  emit stderrChanged();
}

void QsProcess::setRunning(bool running) {
  wantRunning_ = running;
  if (running == (process_ != nullptr)) return;
  if (running)
    startIfReady();
  else
    kill(15);
}

void QsProcess::startIfReady() {
  if (wantRunning_ && !process_ && !command_.isEmpty()) start();
}

void QsProcess::start() {
  if (process_) return;
  wantRunning_ = false;
  killSignal_ = 0;
  auto* process = new QProcess(this);
  CommandSpec spec;
  spec.command = command_;
  spec.environment = environment_;
  spec.clearEnvironment = clearEnvironment_;
  spec.workingDirectory = workingDirectory_;
  process->setProcessEnvironment(makeEnvironment(spec));
  if (!workingDirectory_.isEmpty()) process->setWorkingDirectory(workingDirectory_);
  kernel::hideConsole(*process);
  if (!stdout_) process->setStandardOutputFile(QProcess::nullDevice());
  if (!stderr_) process->setStandardErrorFile(QProcess::nullDevice());
  if (stdout_) stdout_->reset();
  if (stderr_) stderr_->reset();

  connect(process, &QProcess::readyReadStandardOutput, this, [this] { drain(); });
  connect(process, &QProcess::readyReadStandardError, this, [this] { drain(); });
  connect(process, &QProcess::started, this, [this] {
    if (!process_) return;
    pid_ = process_->processId();
    job_ = std::make_unique<ProcessJob>();
    job_->adopt(pid_);  // fails soft: the child then runs outside the job
    if (!stdinEnabled_) process_->closeWriteChannel();
    emit processIdChanged();
    emit started();
  });
  connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    // finished() is not emitted when the program cannot start.
    if (error != QProcess::FailedToStart || !process_) return;
    qWarning() << "Process: could not start" << command_ << process_->errorString();
    process_->deleteLater();
    process_ = nullptr;
    job_.reset();
    pid_ = 0;
    emit processIdChanged();
    emit runningChanged();
  });
  connect(process, &QProcess::finished, this,
          [this](int code, QProcess::ExitStatus status) { onFinished(code, status); });

  process_ = process;
  // Before start(): a program that cannot start reports that from inside start(), and the order
  // of the two changes must be on, then off.
  emit runningChanged();
  process->start(command_.first(), command_.mid(1));
}

void QsProcess::kill(int signal) {
  if (!process_) return;
  if (killSignal_ == 0) killSignal_ = signal;
  if (job_ && job_->adopted())
    job_->terminate();
  else
    process_->kill();
}

void QsProcess::drain() {
  if (!process_) return;
  if (stdout_) {
    const QByteArray out = process_->readAllStandardOutput();
    if (!out.isEmpty()) stdout_->feed(out);
  }
  if (stderr_) {
    const QByteArray err = process_->readAllStandardError();
    if (!err.isEmpty()) stderr_->feed(err);
  }
}

void QsProcess::onFinished(int exitCode, QProcess::ExitStatus status) {
  if (!process_) return;
  // Windows has no signals, so the job's exit code says nothing: a process we killed ends as a
  // crash with the signal number, the way Quickshell reports a signalled process.
  if (killSignal_ != 0) {
    exitCode = killSignal_;
    status = QProcess::CrashExit;
  }
  finishRun(exitCode, static_cast<ExitStatus>(status));
}

void QsProcess::finishRun(int exitCode, ExitStatus status) {
  if (!process_) return;
  drain();
  if (stdout_) stdout_->streamEnded();
  if (stderr_) stderr_->streamEnded();
  if (job_) {
    if (status == NormalExit) job_->release();  // background children may live on
    job_.reset();
  }
  QProcess* finished = process_;
  process_ = nullptr;
  pid_ = 0;
  if (finished->state() == QProcess::NotRunning) {
    finished->deleteLater();
  } else {
    // Still dying (exec() replaces a run without waiting): delete it when it is gone.
    finished->disconnect(this);
    connect(finished, &QProcess::finished, finished, &QObject::deleteLater);
  }
  killSignal_ = 0;
  emit processIdChanged();
  emit runningChanged();
  emit exited(exitCode, status);
}

void QsProcess::exec(const QVariant& context) {
  const auto spec = commandSpecFrom(context);
  if (!spec) {
    qWarning() << "Process.exec: expected a command list or an object with a command";
    return;
  }
  if (process_) {
    // End the old run now instead of waiting for the kill to finish: the GUI thread stays free.
    kill(15);
    finishRun(killSignal_, CrashExit);
  }
  wantRunning_ = false;  // the new run starts below, once everything is applied
  setCommand(spec->command);
  if (context.metaType().id() == QMetaType::QVariantMap) {
    const QVariantMap map = context.toMap();
    if (map.contains(QStringLiteral("environment"))) setEnvironment(spec->environment);
    if (map.contains(QStringLiteral("clearEnvironment")))
      setClearEnvironment(spec->clearEnvironment);
    if (map.contains(QStringLiteral("workingDirectory")))
      setWorkingDirectory(spec->workingDirectory);
  }
  wantRunning_ = true;
  startIfReady();
}

void QsProcess::startDetached() {
  CommandSpec spec;
  spec.command = command_;
  spec.environment = environment_;
  spec.clearEnvironment = clearEnvironment_;
  spec.workingDirectory = workingDirectory_;
  if (!startDetachedSpec(spec)) qWarning() << "Process.startDetached: could not start" << command_;
}

void QsProcess::signal(int signal) {
  if (!process_) return;
  switch (signal) {
    case 2:
    case 3:
    case 9:
    case 15:
      kill(signal);
      break;
    default:
      break;  // Windows has no other signals
  }
}

void QsProcess::write(const QString& data) {
  if (!process_ || !stdinEnabled_ || process_->state() != QProcess::Running) return;
  process_->write(data.toUtf8());
}

}  // namespace wabar::quickshell_io

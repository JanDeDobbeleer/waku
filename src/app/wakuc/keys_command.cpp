#include "app/wakuc/keys_command.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <cstdio>
#include <memory>

#include "control/control.h"
#include "features/keymap/config_watcher.h"
#include "features/keymap/keymap_daemon.h"
#include "features/keymap/keymap_system.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace wakuc {
namespace {

constexpr int kOk = 0;
constexpr int kError = 1;
constexpr int kNotRunning = 2;
constexpr int kUsage = 3;

constexpr int kClientTimeoutMs = 10000;
constexpr int kStartWaitMs = 6000;

void print(FILE* stream, const QString& text) {
  std::fputs(text.toLocal8Bit().constData(), stream);
  std::fputc('\n', stream);
}

// Where the log lines go: stderr in the foreground, and the file given by --log-file. Thread
// safe: the daemon logs from its worker threads, and Qt messages (the kernel hotkey table warns
// about a taken key) come from the GUI thread.
class Sink {
 public:
  void open(const QString& file, bool toStderr) {
    const QMutexLocker lock(&mutex_);
    toStderr_ = toStderr;
    if (!file.isEmpty()) {
      file_ = std::make_unique<QFile>(file);
      if (!file_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (toStderr_)
          print(stderr, QStringLiteral("wakuc: cannot open the log file %1").arg(file));
        file_.reset();
      }
    }
  }
  void write(const QString& line) {
    const QString stamped =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz ")) + line;
    const QMutexLocker lock(&mutex_);
    if (toStderr_) print(stderr, stamped);
    if (file_) {
      file_->write(stamped.toUtf8() + '\n');
      file_->flush();
    }
  }

 private:
  QMutex mutex_;
  bool toStderr_ = false;
  std::unique_ptr<QFile> file_;
};

Sink& sink() {
  static Sink instance;
  return instance;
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
  const char* level = type == QtDebugMsg ? "debug: " : type == QtInfoMsg ? "" : "warning: ";
  sink().write(QString::fromLatin1(level) + message);
}

QString defaultConfigFile() {
  return QDir(QDir::homePath()).filePath(QStringLiteral(".config/waku/keys.conf"));
}

// The daemon's control verbs: the client side. Returns the exit code.
int sendToDaemon(const QString& verb) {
  const waku::control::ControlReply reply =
      waku::control::sendControlCommand(keysPipeName(), verb, kClientTimeoutMs);
  switch (reply.status) {
    case waku::control::ControlReply::NotRunning:
      print(stderr,
            QStringLiteral("wakuc: the key daemon is not running (start it with `wakuc keys`)"));
      return kNotRunning;
    case waku::control::ControlReply::Failed:
      print(stderr, QStringLiteral("wakuc: %1").arg(reply.text));
      return kUsage;
    case waku::control::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kError : kOk;
}

// Starts this program again as the daemon, hidden and detached from this console: a process
// created with CREATE_NO_WINDOW has a console that is never shown, so no window flashes and
// closing the terminal that ran `wakuc keys` does not end it (CREATE_NEW_PROCESS_GROUP keeps
// Ctrl+C away from it too). FreeConsole would leave a flash of the console and nothing to log to.
int startDetached(const KeysOptions& options, const QString& configFile) {
  const waku::control::ControlReply running =
      waku::control::sendControlCommand(keysPipeName(), QStringLiteral("status"), 1000);
  if (running.status == waku::control::ControlReply::Ok) {
    print(stderr, QStringLiteral("wakuc: the key daemon is already running (%1); use `wakuc keys "
                                 "reload` or `wakuc keys stop`")
                      .arg(running.text));
    return kError;
  }
  QStringList args{QStringLiteral("keys"), QStringLiteral("--foreground"),
                   QStringLiteral("--config"), configFile};
  if (!options.logFile.isEmpty()) {
    args << QStringLiteral("--log-file") << QDir::current().absoluteFilePath(options.logFile);
  }
  if (!options.configHome.isEmpty()) {
    args << QStringLiteral("--config-home") << QDir::current().absoluteFilePath(options.configHome);
  }
  QProcess process;
  process.setProgram(QCoreApplication::applicationFilePath());
  process.setArguments(args);
  process.setStandardOutputFile(QProcess::nullDevice());
  process.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
    arguments->flags |= CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
  });
#endif
  if (!process.startDetached()) {
    print(stderr,
          QStringLiteral("wakuc: cannot start the key daemon: %1").arg(process.errorString()));
    return kError;
  }
  // The daemon answers `status` once its event loop runs; a daemon that failed to start never does.
  for (int waited = 0; waited < kStartWaitMs; waited += 100) {
    QThread::msleep(100);
    const waku::control::ControlReply reply =
        waku::control::sendControlCommand(keysPipeName(), QStringLiteral("status"), 1000);
    if (reply.status == waku::control::ControlReply::Ok) {
      print(stdout, QStringLiteral("started: %1").arg(reply.text));
      return kOk;
    }
  }
  print(stderr, QStringLiteral("wakuc: the key daemon did not start; run `wakuc keys --foreground "
                               "--config %1` to see why")
                    .arg(configFile));
  return kError;
}

int runDaemon(const KeysOptions& options, const QString& configFile) {
  sink().open(options.logFile, true);
  qInstallMessageHandler(qtMessageHandler);
  const auto log = [](const QString& line) { sink().write(line); };

  // The control pipe doubles as the single-instance guard (control.h).
  waku::control::ControlHandler handler;
  std::unique_ptr<waku::keymap::KeymapDaemon> daemon;
  handler = [&daemon, &configFile](const QString& command) -> QString {
    if (command == QLatin1String("status")) {
      return QStringLiteral("ok: running; pid=%1; %2; config=%3")
          .arg(QCoreApplication::applicationPid())
          .arg(daemon->statusText(), QDir::toNativeSeparators(configFile));
    }
    if (command == QLatin1String("reload")) return daemon->reloadFile(false).message;
    if (command == QLatin1String("stop")) {
      // Let the answer go out before the loop ends.
      QTimer::singleShot(200, [] { QCoreApplication::quit(); });
      return QStringLiteral("ok: stopping");
    }
    return QStringLiteral("error: unknown command '%1' (reload, stop or status)").arg(command);
  };
  waku::control::ControlServer server(keysPipeName(), handler);
  if (!server.listen()) {
    print(stderr, QStringLiteral("wakuc: the key daemon is already running"));
    return kError;
  }

  const QString barPipe = waku::control::controlPipeName(
      waku::control::resolveConfigHome(options.configHome, QDir::homePath()).path);
  const std::unique_ptr<waku::keymap::ProcessLauncher> launcher =
      waku::keymap::makeWin32ProcessLauncher();
  const std::unique_ptr<waku::keymap::WindowSystem> windows = waku::keymap::makeWin32WindowSystem();
  waku::keymap::PipeControlClient bar(barPipe);

  waku::keymap::KeymapDaemon::Deps deps;
  deps.launcher = launcher.get();
  deps.windows = windows.get();
  deps.bar = &bar;
  deps.log = log;
  deps.stop = [] { QCoreApplication::quit(); };
  daemon = std::make_unique<waku::keymap::KeymapDaemon>(std::move(deps));

  log(QStringLiteral("keys: starting; config %1").arg(QDir::toNativeSeparators(configFile)));
  const waku::keymap::KeymapDaemon::LoadResult first = daemon->loadFile(configFile);
  if (!first.applied) {
    // Nothing to keep yet: a first file that is bad ends the start.
    print(stderr, QStringLiteral("wakuc: %1").arg(first.message));
    return kError;
  }
  // load() logs the outcome of a reload.
  waku::keymap::ConfigWatcher watcher(configFile, [&daemon] { daemon->reloadFile(true); });

  const int code = QCoreApplication::exec();
  daemon.reset();
  QThreadPool::globalInstance()->waitForDone(5000);
  log(QStringLiteral("keys: stopped"));
  return code == 0 ? kOk : kError;
}

}  // namespace

QString keysPipeName() {
  // One daemon per user: the pipe name carries the user name, the instance mutex is per session.
  return waku::control::controlPipeName(QStringLiteral("keys:") + qEnvironmentVariable("USERNAME"));
}

int runKeysCommand(const KeysOptions& options) {
  const bool start = options.verb.isEmpty();
  if (!start && options.verb != QLatin1String("reload") && options.verb != QLatin1String("stop") &&
      options.verb != QLatin1String("status")) {
    print(stderr, QStringLiteral("usage: wakuc keys [--config <file>] [--foreground] [--log-file "
                                 "<file>] | keys reload|stop|status"));
    return kUsage;
  }
  if (!start) {
    if (!options.configFile.isEmpty() || !options.logFile.isEmpty() || options.foreground) {
      print(stderr, QStringLiteral("usage: wakuc keys reload|stop|status take no options"));
      return kUsage;
    }
    return sendToDaemon(options.verb);
  }

  QString configFile = options.configFile;
  if (configFile.isEmpty()) {
    configFile = defaultConfigFile();
    if (!QFileInfo::exists(configFile)) {
      print(stderr, QStringLiteral("wakuc: no key file; pass --config <file> or create %1")
                        .arg(QDir::toNativeSeparators(configFile)));
      return kUsage;
    }
  }
  configFile = QDir::cleanPath(QDir::current().absoluteFilePath(configFile));
  if (!QFileInfo(configFile).isFile()) {
    print(stderr,
          QStringLiteral("wakuc: no such key file: %1").arg(QDir::toNativeSeparators(configFile)));
    return kError;
  }
  return options.foreground ? runDaemon(options, configFile) : startDetached(options, configFile);
}

}  // namespace wakuc

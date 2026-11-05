#include "app/wakukeys/keys_daemon.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "control/control.h"
#include "features/keymap/config_watcher.h"
#include "features/keymap/keymap_daemon.h"
#include "features/keymap/keymap_system.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace wakukeys {
namespace {

constexpr int kOk = 0;
constexpr int kError = 1;
constexpr int kNotRunning = 2;
constexpr int kUsage = 3;

constexpr int kClientTimeoutMs = 10000;
constexpr int kStartWaitMs = 6000;

// wakukeys.exe is a GUI-subsystem program: it starts with no console and no stdio. The first line
// to print attaches to the console of the program that started it (a terminal), and reopens stdout
// and stderr on it when they are not already redirected to a file or a pipe. Started by the
// shell, a task or a double click there is nothing to attach to and the text goes nowhere.
void attachParentConsole() {
#ifdef Q_OS_WIN
  static bool done = false;
  if (done) return;
  done = true;
  AttachConsole(ATTACH_PARENT_PROCESS);
  const auto reopen = [](DWORD which, FILE* stream) {
    const HANDLE handle = GetStdHandle(which);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
      FILE* unused = nullptr;
      freopen_s(&unused, "CONOUT$", "w", stream);
    }
  };
  reopen(STD_OUTPUT_HANDLE, stdout);
  reopen(STD_ERROR_HANDLE, stderr);
#endif
}

void print(FILE* stream, const QString& text) {
  attachParentConsole();
  std::fputs(text.toLocal8Bit().constData(), stream);
  std::fputc('\n', stream);
  std::fflush(stream);
}

// Where the log lines go: the file given by --log-file, nowhere without it. Thread safe: the daemon
// logs from its worker threads, and Qt messages (the kernel hotkey table warns about a taken key)
// come from the GUI thread.
class Sink {
 public:
  void open(const QString& file) {
    const QMutexLocker lock(&mutex_);
    if (!file.isEmpty()) {
      file_ = std::make_unique<QFile>(file);
      if (!file_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        print(stderr, QStringLiteral("wakukeys: cannot open the log file %1").arg(file));
        file_.reset();
      }
    }
  }
  void write(const QString& line) {
    const QString stamped =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz ")) + line;
    const QMutexLocker lock(&mutex_);
    if (file_) {
      file_->write(stamped.toUtf8() + '\n');
      file_->flush();
    }
  }

 private:
  QMutex mutex_;
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

// The key file next to the bar's config: `<config home>/keys.conf`, so a shell that ships one
// gets it without a flag. With no config home given this is ~/.config/waku/keys.conf.
QString defaultConfigFile(const QString& configHomeOption) {
  const QString home = waku::control::resolveConfigHome(configHomeOption, QDir::homePath()).path;
  return QDir(home).filePath(QStringLiteral("keys.conf"));
}

// The daemon's control verbs: the client side. Returns the exit code.
int sendToDaemon(const QString& verb) {
  const waku::control::ControlReply reply =
      waku::control::sendControlCommand(keysPipeName(), verb, kClientTimeoutMs);
  switch (reply.status) {
    case waku::control::ControlReply::NotRunning:
      print(stderr,
            QStringLiteral("wakukeys: the key daemon is not running (start it with `wakukeys`)"));
      return kNotRunning;
    case waku::control::ControlReply::Failed:
      print(stderr, QStringLiteral("wakukeys: %1").arg(reply.text));
      return kUsage;
    case waku::control::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kError : kOk;
}

int runDaemon(const KeysArgs& args, const QString& configFile) {
  sink().open(args.logFile);
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
    // A second start (a login script run twice) shows nothing unless a terminal started it.
    print(stderr, QStringLiteral("wakukeys: already running"));
    return kError;
  }

  const QString barHome = waku::control::resolveConfigHome(args.configHome, QDir::homePath()).path;
  // The commands of the key file run `wakuc`, which finds its bar through WAKU_CONFIG_HOME. Every
  // command inherits this environment, so `--config-home` steers them as it steers `waku:` actions.
  qputenv("WAKU_CONFIG_HOME", QDir::toNativeSeparators(barHome).toLocal8Bit());
  const QString barPipe = waku::control::controlPipeName(barHome);
  const std::unique_ptr<waku::keymap::ProcessLauncher> launcher =
      waku::keymap::makeWin32ProcessLauncher(QCoreApplication::applicationDirPath());
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
    print(stderr, QStringLiteral("wakukeys: %1").arg(first.message));
    return kError;
  }
  // load() logs the outcome of a reload.
  waku::keymap::ConfigWatcher watcher(configFile, [&daemon] { daemon->reloadFile(true); });

  const int code = QCoreApplication::exec();
  daemon.reset();
  QThreadPool::globalInstance()->clear();
  if (!QThreadPool::globalInstance()->waitForDone(5000)) {
    // A bar call (a reload waits up to 60 s) still runs and points at objects of this function.
    // End the process without unwinding them.
    log(QStringLiteral("keys: stopped with a bar call still running"));
    std::quick_exit(code == 0 ? kOk : kError);
  }
  log(QStringLiteral("keys: stopped"));
  return code == 0 ? kOk : kError;
}

}  // namespace

QString keysPipeName() {
  // One daemon per user: the pipe name carries the user name, the instance mutex is per session.
  return waku::control::pipeNameFor(QStringLiteral("keys:") + qEnvironmentVariable("USERNAME"));
}

int runKeys(const KeysArgs& args) {
  if (!args.error.isEmpty()) {
    print(stderr, args.error);
    return kUsage;
  }
  if (!args.verb.isEmpty()) return sendToDaemon(args.verb);

  QString configFile = args.configFile;
  if (configFile.isEmpty()) {
    configFile = defaultConfigFile(args.configHome);
    if (!QFileInfo::exists(configFile)) {
      print(stderr, QStringLiteral("wakukeys: no key file; pass --config <file> or create %1")
                        .arg(QDir::toNativeSeparators(configFile)));
      return kUsage;
    }
  }
  configFile = QDir::cleanPath(QDir::current().absoluteFilePath(configFile));
  if (!QFileInfo(configFile).isFile()) {
    print(
        stderr,
        QStringLiteral("wakukeys: no such key file: %1").arg(QDir::toNativeSeparators(configFile)));
    return kError;
  }
  return runDaemon(args, configFile);
}

}  // namespace wakukeys

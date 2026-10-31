#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace wabar::control {

// The control channel: how `wabarc` (reload, status, quit, wallpaper ...) talks to the running bar.
// A named pipe (QLocalServer) per config home, so two instances with different config homes do not
// meet, and a running instance doubles as the single-instance guard for its config home.
//
// Protocol: the client sends one line (a command), the server answers with one line and closes.
// Commands are answered on the GUI thread of the instance.

// "wabar-" plus a short hash of the config home (case and trailing slashes do not matter).
QString controlPipeName(const QString& configHome);

// The config home to use: the command-line value if given, else $WABAR_CONFIG_HOME, else
// `fallback`.
QString resolveConfigHome(const QString& optionValue, const QString& fallback);

// A command line is one string: words separated by spaces, a word with spaces or quotes is written
// in double quotes and a quote inside is doubled ("My ""best"" pic.jpg"). There are no backslash
// escapes, so a Windows path stays as it is. joinCommand quotes what needs it; splitCommand is
// its inverse and returns false for an unterminated quote.
QString quoteArgument(const QString& argument);
QString joinCommand(const QStringList& words);
bool splitCommand(const QString& line, QStringList* words);

// Answers one command with one line.
using ControlHandler = std::function<QString(const QString& command)>;
// Answers one command with one line, now or later: the handler calls `reply` once, on the
// server's thread. A command that takes long (a reload) answers from a queued call, so it does not
// run inside the socket's read notification.
using ControlReplyFn = std::function<void(const QString& line)>;
using AsyncControlHandler = std::function<void(const QString& command, ControlReplyFn reply)>;

class ControlServer : public QObject {
  Q_OBJECT

 public:
  static constexpr int kMaxCommandBytes = 1024;
  static constexpr int kDefaultIdleTimeoutMs = 3000;
  // An idle timer that fires this much later than due fired after the thread was busy.
  static constexpr int kLateTimerMs = 250;

  // A client that has not finished its line after `idleTimeoutMs` is dropped; one whose line was
  // read waits for its answer however long the command takes. A line that arrived while the
  // thread was busy is answered, not dropped, when the timer fires late.
  ControlServer(QString name, ControlHandler handler, QObject* parent = nullptr,
                int idleTimeoutMs = kDefaultIdleTimeoutMs);
  ControlServer(QString name, AsyncControlHandler handler, QObject* parent = nullptr,
                int idleTimeoutMs = kDefaultIdleTimeoutMs);
  ~ControlServer() override;

  // False when another instance (in this or another process) already serves the name.
  bool listen();
  const QString& name() const { return name_; }

 private:
  void onConnection();
  void serve(QLocalSocket* socket);

  QString name_;
  AsyncControlHandler handler_;
  int idleTimeoutMs_;
  QLocalServer server_;
  // The single-instance guard: a named mutex held while the server lives. Two QLocalServers can
  // listen on one pipe name, so the pipe alone cannot say whether another instance is running.
  void* mutex_ = nullptr;
};

struct ControlReply {
  enum Status { Ok, NotRunning, Failed };
  Status status = Failed;
  QString text;  // the server's line, or a description of what went wrong
};

// Sends `command` to the instance listening on `name` and waits for its answer. Blocking: call it
// off the thread that runs the server (the CLI has no server).
ControlReply sendControlCommand(const QString& name, const QString& command, int timeoutMs = 5000);

}  // namespace wabar::control

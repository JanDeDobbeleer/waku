#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>
#include <functional>

namespace barnacle {

// The control channel: how `barnaclec` (reload, status, quit) talks to the running bar. A named
// pipe (QLocalServer) per config home, so two instances with different config homes do not meet,
// and a running instance doubles as the single-instance guard for its config home.
//
// Protocol: the client sends one line (a command), the server answers with one line and closes.
// Commands are answered on the GUI thread of the instance.

// "barnacle-" plus a short hash of the config home (case and trailing slashes do not matter).
QString controlPipeName(const QString& configHome);

// The config home to use: the command-line value if given, else $BARNACLE_CONFIG_HOME, else
// `fallback`.
QString resolveConfigHome(const QString& optionValue, const QString& fallback);

// Answers one command with one line.
using ControlHandler = std::function<QString(const QString& command)>;

class ControlServer : public QObject {
  Q_OBJECT

 public:
  static constexpr int kMaxCommandBytes = 1024;
  static constexpr int kDefaultIdleTimeoutMs = 3000;

  // A client that has not finished its line after `idleTimeoutMs` is dropped.
  ControlServer(QString name, ControlHandler handler, QObject* parent = nullptr,
                int idleTimeoutMs = kDefaultIdleTimeoutMs);
  ~ControlServer() override;

  // False when another instance (in this or another process) already serves the name.
  bool listen();
  const QString& name() const { return name_; }

 private:
  void onConnection();
  void serve(QLocalSocket* socket);

  QString name_;
  ControlHandler handler_;
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

}  // namespace barnacle

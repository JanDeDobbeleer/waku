#include "control/control.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>
#include <memory>

#include "control/win32/instance_mutex.h"

namespace waku::control {

QString controlPipeName(const QString& configHome) {
  QString normal = QDir::cleanPath(QDir::fromNativeSeparators(configHome)).toLower();
  const QByteArray hash = QCryptographicHash::hash(normal.toUtf8(), QCryptographicHash::Sha1);
  return QStringLiteral("waku-") + QString::fromLatin1(hash.toHex().left(12));
}

QString quoteArgument(const QString& argument) {
  bool plain = !argument.isEmpty();
  for (QChar c : argument) {
    if (c.isSpace() || c == QLatin1Char('"')) plain = false;
  }
  if (plain) return argument;
  QString quoted = argument;
  quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
  return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString joinCommand(const QStringList& words) {
  QStringList quoted;
  for (const QString& word : words) quoted << quoteArgument(word);
  return quoted.join(QLatin1Char(' '));
}

bool splitCommand(const QString& line, QStringList* words) {
  words->clear();
  QString current;
  bool inWord = false;
  bool quoted = false;
  for (qsizetype i = 0; i < line.size(); ++i) {
    const QChar c = line.at(i);
    if (quoted) {
      if (c != QLatin1Char('"')) {
        current += c;
      } else if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
        current += c;
        ++i;
      } else {
        quoted = false;
      }
    } else if (c == QLatin1Char('"')) {
      quoted = true;
      inWord = true;
    } else if (c.isSpace()) {
      if (inWord) words->append(current);
      current.clear();
      inWord = false;
    } else {
      current += c;
      inWord = true;
    }
  }
  if (quoted) return false;
  if (inWord) words->append(current);
  return true;
}

QString resolveConfigHome(const QString& optionValue, const QString& fallback) {
  if (!optionValue.isEmpty()) return optionValue;
  if (const QString fromEnv = qEnvironmentVariable("WAKU_CONFIG_HOME"); !fromEnv.isEmpty()) {
    return fromEnv;
  }
  return fallback;
}

ControlServer::ControlServer(QString name, ControlHandler handler, QObject* parent,
                             int idleTimeoutMs)
    : ControlServer(
          std::move(name),
          [handler = std::move(handler)](const QString& command, const ControlReplyFn& reply) {
            reply(handler(command));
          },
          parent, idleTimeoutMs) {}

ControlServer::ControlServer(QString name, AsyncControlHandler handler, QObject* parent,
                             int idleTimeoutMs)
    : QObject(parent),
      name_(std::move(name)),
      handler_(std::move(handler)),
      idleTimeoutMs_(idleTimeoutMs),
      server_(this) {
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  connect(&server_, &QLocalServer::newConnection, this, &ControlServer::onConnection);
}

ControlServer::~ControlServer() {
  server_.close();
  releaseInstanceMutex(mutex_);
}

bool ControlServer::listen() {
  if (server_.isListening()) return true;
  if (!mutex_) {
    mutex_ = acquireInstanceMutex(name_);
    if (!mutex_) return false;
  }
  // We own the name now, so whatever is left under it is stale.
  QLocalServer::removeServer(name_);
  return server_.listen(name_);
}

void ControlServer::onConnection() {
  while (QLocalSocket* socket = server_.nextPendingConnection()) serve(socket);
}

void ControlServer::serve(QLocalSocket* socket) {
  socket->setParent(this);
  connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
  QPointer<QLocalSocket> guard(socket);
  // A client that never finishes its line must not hold a connection forever. The timer only
  // guards the line: once it has been read, the client waits for its answer (a reload takes
  // seconds), and after the answer the timer runs again as the backstop for a client that never
  // closes.
  auto* idle = new QTimer(socket);
  idle->setSingleShot(true);
  idle->setInterval(idleTimeoutMs_);

  // One command per connection: set once it has been read or refused.
  auto done = std::make_shared<bool>(false);
  auto armed = std::make_shared<QElapsedTimer>();
  auto onReadyRead = [this, socket, guard, idle, armed, done] {
    if (*done) return;
    // Everything read so far stays in the socket's buffer until a full line has arrived.
    if (socket->bytesAvailable() > kMaxCommandBytes + 1) {
      // Not disconnected here: the client closes after reading the error, as for any reply.
      *done = true;
      socket->write("error: command too long\n");
      return;
    }
    if (!socket->canReadLine()) return;
    *done = true;
    idle->stop();
    const QString command = QString::fromUtf8(socket->readLine(kMaxCommandBytes + 1)).trimmed();
    // The client closes once it has read the line. Disconnecting here would let Windows discard
    // the reply before the client reads it (DisconnectNamedPipe drops unread data).
    auto replied = std::make_shared<bool>(false);
    const ControlReplyFn reply = [guard, idle, armed, replied](const QString& line) {
      if (*replied || !guard) return;
      *replied = true;
      guard->write(line.toUtf8() + '\n');
      armed->start();
      idle->start();
    };
    try {
      handler_(command, reply);
    } catch (const std::exception& e) {
      reply(QStringLiteral("error: ") + QString::fromUtf8(e.what()));
    } catch (...) {
      reply(QStringLiteral("error: the command failed"));
    }
  };
  // The thread may have been busy (a reload) when the line arrived: the timer then fires late,
  // before the read notification, which is still queued. A complete line is answered; a timer that
  // fired late gives the client one more interval, so the queued read is taken in first. Only a
  // client that stays silent for a whole interval while the thread is free is dropped.
  armed->start();
  connect(idle, &QTimer::timeout, socket, [this, socket, idle, armed, done, onReadyRead] {
    if (!*done) {
      if (socket->canReadLine()) onReadyRead();
      // Read now: answered (the timer runs again) or waiting for its answer (the timer stopped).
      if (*done) return;
      if (armed->elapsed() > idleTimeoutMs_ + kLateTimerMs) {
        armed->start();
        idle->start();
        return;
      }
    }
    socket->disconnectFromServer();
  });
  idle->start();
  connect(socket, &QLocalSocket::readyRead, socket, onReadyRead);
  // The client may have written before this slot was connected; no further readyRead comes then.
  if (socket->bytesAvailable() > 0) QTimer::singleShot(0, socket, onReadyRead);
}

ControlReply sendControlCommand(const QString& name, const QString& command, int timeoutMs) {
  QLocalSocket socket;
  socket.connectToServer(name);
  if (!socket.waitForConnected(timeoutMs)) {
    const bool absent = socket.error() == QLocalSocket::ServerNotFoundError ||
                        socket.error() == QLocalSocket::ConnectionRefusedError;
    return {absent ? ControlReply::NotRunning : ControlReply::Failed, socket.errorString()};
  }
  socket.write(command.toUtf8() + '\n');
  if (!socket.waitForBytesWritten(timeoutMs)) return {ControlReply::Failed, socket.errorString()};

  // The answer can already be in the socket's buffer: waitForBytesWritten() also takes in what
  // the pipe has received. waitForReadyRead() only waits for more, so it would wait until the
  // server closes the pipe. Read what is there before each wait and after a wait that fails.
  QByteArray received = socket.readAll();
  while (!received.contains('\n')) {
    if (!socket.waitForReadyRead(timeoutMs)) {
      received += socket.readAll();
      if (received.contains('\n')) break;
      if (!received.isEmpty()) break;  // the server closed after a partial line
      return {ControlReply::Failed, QStringLiteral("no answer: ") + socket.errorString()};
    }
    received += socket.readAll();
  }
  return {ControlReply::Ok, QString::fromUtf8(received).trimmed()};
}

}  // namespace waku::control

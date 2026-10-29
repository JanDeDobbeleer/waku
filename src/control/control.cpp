#include "control/control.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>

#include "control/win32/instance_mutex.h"

namespace wabar {

QString controlPipeName(const QString& configHome) {
  QString normal = QDir::cleanPath(QDir::fromNativeSeparators(configHome)).toLower();
  const QByteArray hash = QCryptographicHash::hash(normal.toUtf8(), QCryptographicHash::Sha1);
  return QStringLiteral("wabar-") + QString::fromLatin1(hash.toHex().left(12));
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
  if (const QString fromEnv = qEnvironmentVariable("WABAR_CONFIG_HOME"); !fromEnv.isEmpty()) {
    return fromEnv;
  }
  return fallback;
}

ControlServer::ControlServer(QString name, ControlHandler handler, QObject* parent,
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
  // A client that never finishes its line must not hold a connection forever.
  QPointer<QLocalSocket> guard(socket);
  QTimer::singleShot(idleTimeoutMs_, socket, [guard] {
    if (guard) guard->disconnectFromServer();
  });

  connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
    // Everything read so far stays in the socket's buffer until a full line has arrived.
    if (socket->bytesAvailable() > kMaxCommandBytes + 1) {
      socket->write("error: command too long\n");
      socket->disconnectFromServer();
      return;
    }
    if (!socket->canReadLine()) return;
    const QString command = QString::fromUtf8(socket->readLine(kMaxCommandBytes + 1)).trimmed();
    QString reply;
    try {
      reply = handler_(command);
    } catch (const std::exception& e) {
      reply = QStringLiteral("error: ") + QString::fromUtf8(e.what());
    } catch (...) {
      reply = QStringLiteral("error: the command failed");
    }
    socket->write(reply.toUtf8() + '\n');
    socket->disconnectFromServer();
  });
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

  QByteArray received;
  while (!received.contains('\n')) {
    if (!socket.waitForReadyRead(timeoutMs)) {
      if (socket.state() == QLocalSocket::UnconnectedState && received.contains('\n')) break;
      if (!received.isEmpty()) break;  // the server closed after a partial line
      return {ControlReply::Failed, QStringLiteral("no answer: ") + socket.errorString()};
    }
    received += socket.readAll();
  }
  return {ControlReply::Ok, QString::fromUtf8(received).trimmed()};
}

}  // namespace wabar

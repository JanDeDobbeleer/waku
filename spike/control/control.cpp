#include "spike/control/control.h"

#include <windows.h>

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>

namespace tidy {

QString controlPipeName(const QString& configHome) {
  QString normal = QDir::cleanPath(QDir::fromNativeSeparators(configHome)).toLower();
  const QByteArray hash = QCryptographicHash::hash(normal.toUtf8(), QCryptographicHash::Sha1);
  return QStringLiteral("tidy-") + QString::fromLatin1(hash.toHex().left(12));
}

QString resolveConfigHome(const QString& optionValue, const QString& fallback) {
  if (!optionValue.isEmpty()) return optionValue;
  if (const QString fromEnv = qEnvironmentVariable("TIDY_CONFIG_HOME"); !fromEnv.isEmpty()) {
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
  if (mutex_) CloseHandle(static_cast<HANDLE>(mutex_));
}

bool ControlServer::listen() {
  if (server_.isListening()) return true;
  if (!mutex_) {
    const std::wstring mutexName = L"Local\\" + name_.toStdWString();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(mutex);  // another instance holds it
      return false;
    }
    mutex_ = mutex;
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

}  // namespace tidy

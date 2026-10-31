// GlazewmSocket against a minimal loopback RFC 6455 server (no live GlazeWM involved).
#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <functional>
#include <memory>
#include <vector>

#include "features/workspaces/glazewm.h"

using wabar::workspaces::glazewm::GlazewmSocket;

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!done()) {
    if (timer.elapsed() > timeoutMs) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return true;
}

class FakeWsServer : public QObject {
 public:
  FakeWsServer() {
    listening_ = server_.listen(QHostAddress::LocalHost, 0);
    connect(&server_, &QTcpServer::newConnection, this, [this] { accept(); });
  }
  bool listening() const { return listening_; }
  quint16 port() const { return server_.serverPort(); }
  QString url() const { return QStringLiteral("ws://127.0.0.1:%1").arg(port()); }
  int handshakes() const { return handshakes_; }
  const std::vector<QByteArray>& received() const { return received_; }

  void sendText(const QByteArray& text, int fragmentBytes = 0) {
    if (!client_) return;
    if (fragmentBytes <= 0 || text.size() <= fragmentBytes) {
      client_->write(frame(0x1, true, text));
    } else {
      for (qsizetype pos = 0; pos < text.size(); pos += fragmentBytes) {
        const QByteArray part = text.mid(pos, fragmentBytes);
        const bool last = pos + fragmentBytes >= text.size();
        client_->write(frame(pos == 0 ? 0x1 : 0x0, last, part));
      }
    }
    client_->flush();
  }
  void dropClient() {
    if (client_) client_->abort();
  }

 private:
  static QByteArray frame(int opcode, bool fin, const QByteArray& payload) {
    QByteArray out;
    out.append(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
    const quint64 n = static_cast<quint64>(payload.size());
    if (n < 126) {
      out.append(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
      out.append(static_cast<char>(126));
      out.append(static_cast<char>(n >> 8));
      out.append(static_cast<char>(n & 0xFF));
    } else {
      out.append(static_cast<char>(127));
      for (int shift = 56; shift >= 0; shift -= 8)
        out.append(static_cast<char>((n >> shift) & 0xFF));
    }
    out.append(payload);
    return out;
  }

  void accept() {
    while (QTcpSocket* socket = server_.nextPendingConnection()) {
      client_ = socket;
      upgraded_ = false;
      buffer_.clear();
      connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        if (socket != client_) return;
        buffer_ += socket->readAll();
        if (!upgraded_) handshake(socket);
        if (upgraded_) parseFrames();
      });
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      connect(socket, &QObject::destroyed, this, [this, socket] {
        if (client_ == socket) client_ = nullptr;
      });
    }
  }

  void handshake(QTcpSocket* socket) {
    const qsizetype end = buffer_.indexOf("\r\n\r\n");
    if (end < 0) return;
    const QByteArray head = buffer_.left(end);
    buffer_.remove(0, end + 4);
    QByteArray key;
    for (const QByteArray& line : head.split('\n')) {
      const QByteArray trimmed = line.trimmed();
      if (trimmed.toLower().startsWith("sec-websocket-key:")) {
        key = trimmed.mid(trimmed.indexOf(':') + 1).trimmed();
      }
    }
    const QByteArray accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                       QCryptographicHash::Sha1)
                                  .toBase64();
    socket->write(
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
        accept + "\r\n\r\n");
    socket->flush();
    upgraded_ = true;
    ++handshakes_;
  }

  void parseFrames() {
    for (;;) {
      if (buffer_.size() < 2) return;
      const auto byte = [this](qsizetype i) { return static_cast<quint8>(buffer_[i]); };
      const int opcode = byte(0) & 0x0F;
      const bool masked = (byte(1) & 0x80) != 0;
      quint64 len = byte(1) & 0x7F;
      qsizetype pos = 2;
      if (len == 126) {
        if (buffer_.size() < 4) return;
        len = (quint64(byte(2)) << 8) | byte(3);
        pos = 4;
      } else if (len == 127) {
        if (buffer_.size() < 10) return;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | byte(2 + i);
        pos = 10;
      }
      const qsizetype maskLen = masked ? 4 : 0;
      if (static_cast<quint64>(buffer_.size()) < static_cast<quint64>(pos + maskLen) + len) return;
      QByteArray payload = buffer_.mid(pos + maskLen, static_cast<qsizetype>(len));
      if (masked) {
        for (qsizetype i = 0; i < payload.size(); ++i) payload[i] ^= buffer_[pos + (i % 4)];
      }
      buffer_.remove(0, pos + maskLen + static_cast<qsizetype>(len));
      if (opcode == 0x1) received_.push_back(payload);
    }
  }

  QTcpServer server_;
  bool listening_ = false;
  QTcpSocket* client_ = nullptr;
  QByteArray buffer_;
  bool upgraded_ = false;
  int handshakes_ = 0;
  std::vector<QByteArray> received_;
};

struct Recorder {
  explicit Recorder(GlazewmSocket& socket) {
    QObject::connect(&socket, &GlazewmSocket::connectedChanged, &socket,
                     [this](bool state) { states.push_back(state); });
    QObject::connect(&socket, &GlazewmSocket::textMessage, &socket,
                     [this](const QString& text) { texts.push_back(text); });
  }
  std::vector<bool> states;
  std::vector<QString> texts;
};

GlazewmSocket::Timing fastTiming() { return {100, 2000}; }

}  // namespace

TEST(GlazewmSocket, ConnectsAndReportsState) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket socket(fastTiming(), nullptr);
  Recorder rec(socket);
  socket.open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return !rec.states.empty(); }));
  EXPECT_TRUE(rec.states.front());
  EXPECT_TRUE(socket.isConnected());
  EXPECT_EQ(server.handshakes(), 1);
  socket.stop();
  EXPECT_FALSE(socket.isConnected());
  socket.stop();  // idempotent
}

TEST(GlazewmSocket, ReceivesTextIncludingLargeAndFragmented) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket socket(fastTiming(), nullptr);
  Recorder rec(socket);
  socket.open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return socket.isConnected(); }));

  server.sendText("{\"hello\":1}");
  const QByteArray large(200 * 1024, 'x');
  server.sendText(large);
  QByteArray fragmented;
  for (int i = 0; i < 3000; ++i) fragmented.append("ab\xC3\xA9");  // multi-byte UTF-8
  server.sendText(fragmented, 777);
  ASSERT_TRUE(pumpUntil([&] { return rec.texts.size() >= 3; }));
  EXPECT_EQ(rec.texts[0], QStringLiteral("{\"hello\":1}"));
  EXPECT_EQ(rec.texts[1].toUtf8(), large);
  EXPECT_EQ(rec.texts[2].toUtf8(), fragmented);
}

TEST(GlazewmSocket, SendTextRoundTrip) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket socket(fastTiming(), nullptr);
  socket.open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return socket.isConnected(); }));
  EXPECT_TRUE(socket.sendText("sub -e window_managed"));
  const QByteArray big(100 * 1024, 'y');
  EXPECT_TRUE(socket.sendText(big));
  ASSERT_TRUE(pumpUntil([&] { return server.received().size() >= 2; }));
  EXPECT_EQ(server.received()[0], QByteArray("sub -e window_managed"));
  EXPECT_EQ(server.received()[1], big);
}

TEST(GlazewmSocket, SendWhileDisconnectedIsDropped) {
  GlazewmSocket socket(fastTiming(), nullptr);
  EXPECT_FALSE(socket.sendText("query monitors"));
}

TEST(GlazewmSocket, ReconnectsAfterServerClosesConnection) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket socket(fastTiming(), nullptr);
  Recorder rec(socket);
  socket.open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return rec.states.size() >= 1; }));
  server.dropClient();
  ASSERT_TRUE(pumpUntil([&] { return rec.states.size() >= 3; }));
  EXPECT_EQ(rec.states, (std::vector<bool>{true, false, true}));
  EXPECT_EQ(server.handshakes(), 2);
  server.sendText("again");
  ASSERT_TRUE(pumpUntil([&] { return !rec.texts.empty(); }));
  EXPECT_EQ(rec.texts.front(), QStringLiteral("again"));
}

TEST(GlazewmSocket, StopWhileConnectingToDeadPortReturnsPromptly) {
  quint16 deadPort = 0;
  {
    QTcpServer probe;
    ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
    deadPort = probe.serverPort();
  }
  GlazewmSocket socket;  // default 4 s reconnect delay must not delay stop()
  socket.open(QStringLiteral("ws://127.0.0.1:%1").arg(deadPort));
  QThread::msleep(300);
  QElapsedTimer timer;
  timer.start();
  socket.stop();
  EXPECT_LT(timer.elapsed(), 2000);
  EXPECT_FALSE(socket.isConnected());
}

TEST(GlazewmSocket, StaysConnectedWhileIdle) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket socket(fastTiming(), nullptr);
  Recorder rec(socket);
  socket.open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return socket.isConnected(); }));
  pumpUntil([] { return false; }, 400);
  server.sendText("late");
  ASSERT_TRUE(pumpUntil([&] { return !rec.texts.empty(); }));
  EXPECT_EQ(rec.texts.front(), QStringLiteral("late"));
  EXPECT_EQ(server.handshakes(), 1);
  EXPECT_EQ(rec.states, (std::vector<bool>{true}));
}

TEST(GlazewmSocket, StopWhileConnectedReturnsPromptly) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket socket(fastTiming(), nullptr);
  socket.open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return socket.isConnected(); }));
  QElapsedTimer timer;
  timer.start();
  socket.stop();
  EXPECT_LT(timer.elapsed(), 1000);
  EXPECT_FALSE(socket.isConnected());
}

TEST(GlazewmSocket, StopDoesNotWaitAndNothingArrivesAfterIt) {
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  auto socket = std::make_unique<GlazewmSocket>(fastTiming(), nullptr);
  Recorder rec(*socket);
  socket->open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return socket->isConnected(); }));
  server.sendText("before");
  ASSERT_TRUE(pumpUntil([&] { return !rec.texts.empty(); }));
  server.sendText("queued");
  QThread::msleep(100);  // the worker posts the text; the loop has not delivered it yet
  QElapsedTimer timer;
  timer.start();
  socket->stop();
  EXPECT_LT(timer.elapsed(), 1000);
  const auto states = rec.states;
  const auto texts = rec.texts;
  pumpUntil([] { return false; }, 300);
  EXPECT_EQ(rec.states, states);
  EXPECT_EQ(rec.texts, texts);
  // The socket goes while nothing waited for its worker; a new open starts a new one.
  socket->open(server.url());
  ASSERT_TRUE(pumpUntil([&] { return socket->isConnected(); }));
  socket.reset();
  pumpUntil([] { return false; }, 200);
}

TEST(GlazewmSocket, NoConnectWhileNothingListensOnALocalPort) {
  quint16 deadPort = 0;
  {
    QTcpServer probe;
    ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
    deadPort = probe.serverPort();
  }
  GlazewmSocket socket(fastTiming(), nullptr);
  socket.open(QStringLiteral("ws://127.0.0.1:%1").arg(deadPort));
  QThread::msleep(300);
  // The worker sleeps between probes instead of waiting in a refused connect: stop is immediate.
  QElapsedTimer timer;
  timer.start();
  socket.stop();
  EXPECT_LT(timer.elapsed(), 100);
  // Something starts listening: the next probe connects.
  FakeWsServer server;
  ASSERT_TRUE(server.listening());
  GlazewmSocket later(fastTiming(), nullptr);
  later.open(server.url());
  EXPECT_TRUE(pumpUntil([&] { return later.isConnected(); }));
}
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <thread>

namespace wabar::workspaces::glazewm {

// Transport half of GlazeWM's IPC client (Python: glazewm/client.py): a plain `ws://` WebSocket
// client on the documented WinHTTP WebSocket API. Text frames only; fragmented messages are
// reassembled; the connection is retried every `reconnectMs` until stop().
//
// Threading: open() spawns one std::thread (the worker). The worker owns connecting, the blocking
// receive loop and the reconnect sleep, and is the only thread that creates or closes WinHTTP
// handles while it runs (WinHTTP forbids closing a handle under a synchronous call on another
// thread). The handles are published under `mutex_`. sendText() runs on the caller's thread and
// holds `mutex_` across WinHttpWebSocketSend, so the worker cannot close the socket under it (a
// send is safe next to a pending receive). Signals are emitted by queued invocation, so receivers
// on the GUI thread see `connectedChanged` and `textMessage` in order. stop() (also from the
// destructor) sets the stop flag and event, ends a blocked receive with WinHttpWebSocketClose
// (documented to cancel it; waits at most 250 ms for the close handshake) and joins the worker; a
// connect in progress ends within `connectTimeoutMs`. It is idempotent. A connectedChanged(false)
// still queued when stop() returns is delivered later unless the object is destroyed.
class GlazewmSocket : public QObject {
  Q_OBJECT
 public:
  struct Timing {
    int reconnectMs = 4000;
    // Bounds stop() while connecting; GlazeWM listens on localhost.
    int connectTimeoutMs = 2000;
  };

  explicit GlazewmSocket(QObject* parent = nullptr);
  GlazewmSocket(Timing timing, QObject* parent);
  ~GlazewmSocket() override;

  // Starts connecting to `url` (ws:// or wss://). No-op while already running.
  void open(const QString& url);
  // Sends one UTF-8 text frame. False (message dropped) when not connected or the send failed.
  bool sendText(const QByteArray& utf8);
  void stop();
  bool isConnected() const { return connected_.load(); }

  static constexpr qsizetype kMaxMessageBytes = 64 * 1024 * 1024;

 signals:
  void connectedChanged(bool connected);
  void textMessage(const QString& text);

 private:
  struct Handles {
    void* session = nullptr;
    void* connect = nullptr;
    void* request = nullptr;
    void* webSocket = nullptr;
  };
  enum class Slot { Session, Connect, Request, WebSocket };

  void run();
  bool connectOnce();
  void receiveLoop(void* webSocket);
  bool publish(Slot slot, void* handle);
  void closeSlot(Slot slot);
  void closeAll();
  bool sleep(int ms);
  void emitConnected(bool state);
  void emitText(QByteArray utf8);

  Timing timing_;
  QString url_;
  std::thread thread_;
  void* stopEvent_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> connected_{false};
  std::mutex mutex_;
  Handles handles_;
};

}  // namespace wabar::workspaces::glazewm

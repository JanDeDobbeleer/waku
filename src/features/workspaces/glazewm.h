#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

namespace waku::workspaces::glazewm {

// Transport half of GlazeWM's IPC client (Python: glazewm/client.py): a plain `ws://` WebSocket
// client on the documented WinHTTP WebSocket API. Text frames only; fragmented messages are
// reassembled; the connection is retried every `reconnectMs` until stop(). A localhost URL is
// only connected to while something listens on its port, so no connect waits on a closed port
// when GlazeWM is not running.
//
// Threading: the socket lives on the GUI thread. open() spawns one std::thread (the worker) that
// shares a state block with the socket. The worker owns connecting, the blocking receive loop and
// the reconnect sleep, and is the only thread that creates or closes WinHTTP handles while it runs
// (WinHTTP forbids closing a handle under a synchronous call on another thread). The handles are
// published under the state's mutex. sendText() holds that mutex across WinHttpWebSocketSend, so
// the worker cannot close the socket under it (a send is safe next to a pending receive). Signals
// are posted to the GUI thread in order (`connectedChanged`, `textMessage`). stop() (also from
// the destructor) sets the stop flag and event and ends a blocked receive with
// WinHttpWebSocketClose (documented to cancel it; waits at most 250 ms for the close handshake).
// It does not join: a connect in progress ends within `connectTimeoutMs` on the worker, which then
// closes its handles and ends; the state block lives until then. Nothing is emitted after stop().
// It is idempotent, and open() after stop() starts a new worker. At exit the process waits up to
// 2 s for workers that have not ended.
class GlazewmSocket : public QObject {
  Q_OBJECT
 public:
  struct Timing {
    int reconnectMs = 4000;
    // Bounds stop() while connecting; GlazeWM listens on localhost.
    int connectTimeoutMs = 2000;
    // A failed attempt (GlazeWM not running) doubles the wait up to this; a connection that was
    // made starts it over at `reconnectMs`.
    int reconnectMaxMs = 30000;
  };

  explicit GlazewmSocket(QObject* parent = nullptr);
  GlazewmSocket(Timing timing, QObject* parent);
  ~GlazewmSocket() override;

  // Starts connecting to `url` (ws:// or wss://). No-op while already running.
  void open(const QString& url);
  // Sends one UTF-8 text frame. False (message dropped) when not connected or the send failed.
  bool sendText(const QByteArray& utf8);
  void stop();
  bool isConnected() const;

  static constexpr qsizetype kMaxMessageBytes = 64 * 1024 * 1024;

 signals:
  void connectedChanged(bool connected);
  void textMessage(const QString& text);

 private:
  struct Worker;  // the state the worker thread shares; defined in win32/glazewm_socket.cpp

  Timing timing_;
  std::shared_ptr<Worker> worker_;
};

}  // namespace waku::workspaces::glazewm

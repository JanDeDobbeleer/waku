// clang-format off
// iphlpapi.h needs the socket address types first (types only; no Winsock call is made).
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winhttp.h>
// clang-format on

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QMetaObject>
#include <QUrl>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "features/workspaces/glazewm.h"

namespace waku::workspaces::glazewm {
namespace {

constexpr DWORD kChunkBytes = 64 * 1024;
// How long stop() waits for GlazeWM to answer the close frame.
constexpr DWORD kCloseHandshakeMs = 250;
// How long the process waits at exit for each worker that has not ended yet.
constexpr DWORD kExitWaitMs = 2000;

bool isLoopbackHost(const QString& host) {
  return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
         host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1") ||
         host == QStringLiteral("[::1]");
}

// The table's port fields hold the port in network byte order in their low 16 bits.
unsigned short hostPort(DWORD field) {
  return static_cast<unsigned short>(((field & 0xFF) << 8) | ((field >> 8) & 0xFF));
}

// Whether a TCP socket listens on `port` (IPv4 or IPv6), from the documented TCP tables
// (GetExtendedTcpTable). True when a table cannot be read, so a failure here never stops a
// connect.
bool somethingListensOn(unsigned short port) {
  for (const ULONG family : {static_cast<ULONG>(AF_INET), static_cast<ULONG>(AF_INET6)}) {
    DWORD bytes = 0;
    DWORD status =
        GetExtendedTcpTable(nullptr, &bytes, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (status != ERROR_INSUFFICIENT_BUFFER) return true;
    // Room for sockets that start listening between the two calls.
    std::vector<std::byte> buffer(bytes + 1024);
    bytes = static_cast<DWORD>(buffer.size());
    status =
        GetExtendedTcpTable(buffer.data(), &bytes, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (status != NO_ERROR) return true;
    if (family == AF_INET) {
      const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
      for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        if (hostPort(table->table[i].dwLocalPort) == port) return true;
      }
    } else {
      const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
      for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        if (hostPort(table->table[i].dwLocalPort) == port) return true;
      }
    }
  }
  return false;
}

}  // namespace

// Shared by the socket (GUI thread) and its worker thread; the worker holds a reference until it
// ends, so a stopped socket can go while its worker still finishes a connect.
struct GlazewmSocket::Worker : std::enable_shared_from_this<Worker> {
  struct Handles {
    void* session = nullptr;
    void* connect = nullptr;
    void* request = nullptr;
    void* webSocket = nullptr;
  };
  enum class Slot { Session, Connect, Request, WebSocket };

  ~Worker() {
    if (stopEvent) CloseHandle(stopEvent);
    if (doneEvent) CloseHandle(doneEvent);
  }

  void run();
  bool connectOnce();
  void receiveLoop(void* webSocket);
  bool publish(Slot slot, void* handle);
  void closeSlot(Slot slot);
  void closeAll();
  bool sleep(int ms);
  void emitConnected(bool state);
  void emitText(QByteArray utf8);

  // Workers whose socket was stopped but which may still be running. GUI thread.
  static QList<std::weak_ptr<Worker>>& retiring() {
    static QList<std::weak_ptr<Worker>> workers;
    return workers;
  }
  static void waitAtExit() {
    for (const std::weak_ptr<Worker>& weak : std::as_const(retiring())) {
      if (const std::shared_ptr<Worker> worker = weak.lock()) {
        WaitForSingleObject(worker->doneEvent, kExitWaitMs);
      }
    }
  }

  Timing timing;
  QString url;
  GlazewmSocket* owner = nullptr;  // dereferenced on the GUI thread only, and only before stop()
  HANDLE stopEvent = nullptr;
  HANDLE doneEvent = nullptr;  // set when run() has returned
  std::atomic<bool> stopping{false};
  std::atomic<bool> connected{false};
  std::mutex mutex;
  Handles handles;
};

GlazewmSocket::GlazewmSocket(QObject* parent) : QObject(parent) {}

GlazewmSocket::GlazewmSocket(Timing timing, QObject* parent) : QObject(parent), timing_(timing) {}

GlazewmSocket::~GlazewmSocket() { stop(); }

bool GlazewmSocket::isConnected() const { return worker_ && worker_->connected.load(); }

void GlazewmSocket::open(const QString& url) {
  if (worker_) return;
  auto worker = std::make_shared<Worker>();
  worker->timing = timing_;
  worker->url = url;
  worker->owner = this;
  worker->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  worker->doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!worker->stopEvent || !worker->doneEvent) {
    qWarning() << "GlazeWM socket could not create its events:" << GetLastError();
    return;
  }
  try {
    std::thread([worker] {
      worker->run();
      SetEvent(worker->doneEvent);
    }).detach();
  } catch (const std::exception& e) {
    qWarning() << "GlazeWM socket could not start its thread:" << e.what();
    return;
  }
  worker_ = std::move(worker);
}

void GlazewmSocket::stop() {
  if (!worker_) return;
  const std::shared_ptr<Worker> worker = std::move(worker_);
  // WinHTTP forbids closing a handle while another thread is inside a synchronous call on it, so
  // the worker closes its own handles. A blocked receive is ended the documented way:
  // WinHttpWebSocketClose makes it return ERROR_WINHTTP_OPERATION_CANCELLED. It waits at most
  // kCloseHandshakeMs for GlazeWM's answer. Held under the mutex, so no send runs next to it and
  // the worker cannot close the socket meanwhile. A connect in progress is not waited for: the
  // worker sees the flag when it returns.
  worker->stopping = true;
  worker->connected = false;
  SetEvent(worker->stopEvent);
  {
    std::lock_guard<std::mutex> lock(worker->mutex);
    if (worker->handles.webSocket) {
      WinHttpWebSocketClose(worker->handles.webSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                            nullptr, 0);
    }
  }
  if (WaitForSingleObject(worker->doneEvent, 0) == WAIT_OBJECT_0) return;
  static const bool registered = [] {
    qAddPostRoutine(&Worker::waitAtExit);
    return true;
  }();
  Q_UNUSED(registered);
  QList<std::weak_ptr<Worker>>& retiring = Worker::retiring();
  retiring.removeIf([](const std::weak_ptr<Worker>& weak) { return weak.expired(); });
  retiring.append(worker);
}

bool GlazewmSocket::sendText(const QByteArray& utf8) {
  if (!worker_) return false;
  try {
    // Held across the send so the worker cannot close the handle under it (closeSlot takes it).
    std::lock_guard<std::mutex> lock(worker_->mutex);
    void* webSocket = worker_->handles.webSocket;
    if (!webSocket || !worker_->connected) return false;
    const DWORD error =
        WinHttpWebSocketSend(webSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                             const_cast<char*>(utf8.constData()), static_cast<DWORD>(utf8.size()));
    if (error != ERROR_SUCCESS) {
      qWarning() << "GlazeWM socket send failed:" << error;
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    qWarning() << "GlazeWM socket send threw:" << e.what();
    return false;
  }
}

bool GlazewmSocket::Worker::sleep(int ms) {
  return WaitForSingleObject(stopEvent, static_cast<DWORD>(ms < 0 ? 0 : ms)) == WAIT_TIMEOUT;
}

// The stop flag is read again on the GUI thread, where stop() sets it: an emission queued before
// stop() never reaches a socket that has been stopped or destroyed.
void GlazewmSocket::Worker::emitConnected(bool state) {
  if (stopping) return;
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [self = shared_from_this(), state] {
        if (!self->stopping) emit self->owner->connectedChanged(state);
      },
      Qt::QueuedConnection);
}

void GlazewmSocket::Worker::emitText(QByteArray utf8) {
  if (stopping) return;
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [self = shared_from_this(), text = QString::fromUtf8(utf8)] {
        if (!self->stopping) emit self->owner->textMessage(text);
      },
      Qt::QueuedConnection);
}

bool GlazewmSocket::Worker::publish(Slot slot, void* handle) {
  std::lock_guard<std::mutex> lock(mutex);
  if (stopping) {
    WinHttpCloseHandle(handle);
    return false;
  }
  switch (slot) {
    case Slot::Session:
      handles.session = handle;
      break;
    case Slot::Connect:
      handles.connect = handle;
      break;
    case Slot::Request:
      handles.request = handle;
      break;
    case Slot::WebSocket:
      handles.webSocket = handle;
      break;
  }
  return true;
}

void GlazewmSocket::Worker::closeSlot(Slot slot) {
  void* handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex);
    switch (slot) {
      case Slot::Session:
        std::swap(handle, handles.session);
        break;
      case Slot::Connect:
        std::swap(handle, handles.connect);
        break;
      case Slot::Request:
        std::swap(handle, handles.request);
        break;
      case Slot::WebSocket:
        std::swap(handle, handles.webSocket);
        break;
    }
  }
  if (handle) WinHttpCloseHandle(handle);
}

void GlazewmSocket::Worker::closeAll() {
  // Children first; closing a handle aborts any call blocked on it.
  closeSlot(Slot::WebSocket);
  closeSlot(Slot::Request);
  closeSlot(Slot::Connect);
  closeSlot(Slot::Session);
}

void GlazewmSocket::Worker::run() {
  bool warned = false;
  const QUrl parsed(url);
  const bool probe = isLoopbackHost(parsed.host());
  const auto port =
      static_cast<unsigned short>(parsed.port(parsed.scheme().toLower() == "wss" ? 443 : 80));
  while (!stopping) {
    try {
      // Nothing listens on a localhost port while GlazeWM is not running: a connect would only
      // wait out Windows' retries on the refused port (about 2 s), every reconnect.
      const bool ok = (!probe || somethingListensOn(port)) && connectOnce();
      if (ok) {
        warned = false;
        void* webSocket = nullptr;
        {
          std::lock_guard<std::mutex> lock(mutex);
          webSocket = handles.webSocket;
        }
        connected = true;
        emitConnected(true);
        if (webSocket) receiveLoop(webSocket);
        connected = false;
        emitConnected(false);
      } else if (!stopping && !warned) {
        qInfo() << "GlazeWM socket could not connect to" << url << "; retrying";
        warned = true;
      }
    } catch (const std::exception& e) {
      qWarning() << "GlazeWM socket worker error:" << e.what();
      if (connected.exchange(false)) emitConnected(false);
    } catch (...) {
      qWarning() << "GlazeWM socket worker unknown error";
      if (connected.exchange(false)) emitConnected(false);
    }
    closeAll();
    if (stopping) break;
    if (!sleep(timing.reconnectMs)) break;
  }
  closeAll();
}

bool GlazewmSocket::Worker::connectOnce() {
  const QUrl parsed(url);
  const QString scheme = parsed.scheme().toLower();
  if (!parsed.isValid() || parsed.host().isEmpty() ||
      (scheme != QStringLiteral("ws") && scheme != QStringLiteral("wss"))) {
    qWarning() << "GlazeWM socket got an invalid URL:" << url;
    return false;
  }
  const bool secure = scheme == QStringLiteral("wss");
  const INTERNET_PORT port = static_cast<INTERNET_PORT>(parsed.port(secure ? 443 : 80));
  QString target = parsed.path(QUrl::FullyEncoded);
  if (target.isEmpty()) target = QStringLiteral("/");
  if (parsed.hasQuery()) target += QLatin1Char('?') + parsed.query(QUrl::FullyEncoded);
  const std::wstring host = parsed.host().toStdWString();
  const std::wstring path = target.toStdWString();

  HINTERNET session = WinHttpOpen(L"waku", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  if (!publish(Slot::Session, session)) return false;
  const int t = timing.connectTimeoutMs;
  WinHttpSetTimeouts(session, t, t, t, t);

  HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
  if (!connect || !publish(Slot::Connect, connect)) return false;

  HINTERNET request =
      WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request || !publish(Slot::Request, request)) return false;

  if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return false;
  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                          0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    return false;
  }
  HINTERNET webSocket = WinHttpWebSocketCompleteUpgrade(request, 0);
  if (!webSocket) return false;
  // The upgrade consumed the request handle's role; release it before publishing the socket.
  closeSlot(Slot::Request);
  if (!publish(Slot::WebSocket, webSocket)) return false;
  // Idle sockets must not time out: GlazeWM only sends on events. The receive blocks until one
  // arrives or stop() closes the socket.
  DWORD infinite = 0;
  WinHttpSetOption(webSocket, WINHTTP_OPTION_RECEIVE_TIMEOUT, &infinite, sizeof(infinite));
  // Bounds the close handshake of stop() (default 10 s) against a peer that never answers it.
  DWORD closeTimeout = kCloseHandshakeMs;
  if (!WinHttpSetOption(webSocket, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &closeTimeout,
                        sizeof(closeTimeout))) {
    qDebug() << "GlazeWM socket could not shorten its close timeout:" << GetLastError();
  }
  return true;
}

void GlazewmSocket::Worker::receiveLoop(void* webSocket) {
  std::vector<char> chunk(kChunkBytes);
  QByteArray message;
  bool binary = false;
  bool overflow = false;
  while (!stopping) {
    DWORD read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
    const DWORD error = WinHttpWebSocketReceive(webSocket, chunk.data(), kChunkBytes, &read, &type);
    if (error == ERROR_WINHTTP_TIMEOUT) continue;
    if (error != ERROR_SUCCESS) {
      if (!stopping) qWarning() << "GlazeWM socket receive failed:" << error;
      return;
    }
    if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
      qInfo() << "GlazeWM socket closed by peer";
      return;
    }
    const bool isBinary = type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                          type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
    const bool isFinal = type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                         type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
    if (message.isEmpty() && !overflow) binary = isBinary;
    if (!binary && !overflow) {
      if (message.size() + static_cast<qsizetype>(read) > kMaxMessageBytes) {
        qWarning() << "GlazeWM socket message exceeds" << kMaxMessageBytes << "bytes; dropping it";
        overflow = true;
        message.clear();
      } else {
        message.append(chunk.data(), static_cast<qsizetype>(read));
      }
    }
    if (!isFinal) continue;
    if (!binary && !overflow) {
      QByteArray complete;
      complete.swap(message);
      emitText(std::move(complete));
    }
    message.clear();
    binary = false;
    overflow = false;
  }
}

}  // namespace waku::workspaces::glazewm

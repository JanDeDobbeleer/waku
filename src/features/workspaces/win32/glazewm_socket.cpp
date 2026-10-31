#include <windows.h>
#include <winhttp.h>

#include <QDebug>
#include <QMetaObject>
#include <QUrl>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "features/workspaces/glazewm.h"

namespace wabar::workspaces::glazewm {
namespace {

constexpr DWORD kChunkBytes = 64 * 1024;
// How long stop() waits for GlazeWM to answer the close frame.
constexpr DWORD kCloseHandshakeMs = 250;

}  // namespace

GlazewmSocket::GlazewmSocket(QObject* parent) : QObject(parent) {}

GlazewmSocket::GlazewmSocket(Timing timing, QObject* parent) : QObject(parent), timing_(timing) {}

GlazewmSocket::~GlazewmSocket() { stop(); }

void GlazewmSocket::open(const QString& url) {
  if (thread_.joinable()) return;
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stopEvent_) {
    qWarning() << "GlazeWM socket could not create its stop event:" << GetLastError();
    return;
  }
  url_ = url;
  stopping_ = false;
  try {
    thread_ = std::thread([this] { run(); });
  } catch (const std::exception& e) {
    qWarning() << "GlazeWM socket could not start its thread:" << e.what();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
  }
}

void GlazewmSocket::stop() {
  if (!thread_.joinable()) return;
  // WinHTTP forbids closing a handle while another thread is inside a synchronous call on it, so
  // the worker closes its own handles. A blocked receive is ended the documented way:
  // WinHttpWebSocketClose makes it return ERROR_WINHTTP_OPERATION_CANCELLED. It waits at most
  // kCloseHandshakeMs for GlazeWM's answer. Held under mutex_, so no send runs next to it and the
  // worker cannot close the socket meanwhile. A connect in progress ends within its timeout.
  stopping_ = true;
  SetEvent(stopEvent_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handles_.webSocket) {
      WinHttpWebSocketClose(handles_.webSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr,
                            0);
    }
  }
  thread_.join();
  closeAll();
  CloseHandle(stopEvent_);
  stopEvent_ = nullptr;
  connected_ = false;
}

bool GlazewmSocket::sendText(const QByteArray& utf8) {
  try {
    // Held across the send so the worker cannot close the handle under it (closeSlot takes it).
    std::lock_guard<std::mutex> lock(mutex_);
    void* webSocket = handles_.webSocket;
    if (!webSocket || !connected_) return false;
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

bool GlazewmSocket::sleep(int ms) {
  return WaitForSingleObject(stopEvent_, static_cast<DWORD>(ms < 0 ? 0 : ms)) == WAIT_TIMEOUT;
}

void GlazewmSocket::emitConnected(bool state) {
  QMetaObject::invokeMethod(
      this, [this, state] { emit connectedChanged(state); }, Qt::QueuedConnection);
}

void GlazewmSocket::emitText(QByteArray utf8) {
  QMetaObject::invokeMethod(
      this, [this, text = QString::fromUtf8(utf8)] { emit textMessage(text); },
      Qt::QueuedConnection);
}

bool GlazewmSocket::publish(Slot slot, void* handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_) {
    WinHttpCloseHandle(handle);
    return false;
  }
  switch (slot) {
    case Slot::Session:
      handles_.session = handle;
      break;
    case Slot::Connect:
      handles_.connect = handle;
      break;
    case Slot::Request:
      handles_.request = handle;
      break;
    case Slot::WebSocket:
      handles_.webSocket = handle;
      break;
  }
  return true;
}

void GlazewmSocket::closeSlot(Slot slot) {
  void* handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (slot) {
      case Slot::Session:
        std::swap(handle, handles_.session);
        break;
      case Slot::Connect:
        std::swap(handle, handles_.connect);
        break;
      case Slot::Request:
        std::swap(handle, handles_.request);
        break;
      case Slot::WebSocket:
        std::swap(handle, handles_.webSocket);
        break;
    }
  }
  if (handle) WinHttpCloseHandle(handle);
}

void GlazewmSocket::closeAll() {
  // Children first; closing a handle aborts any call blocked on it.
  closeSlot(Slot::WebSocket);
  closeSlot(Slot::Request);
  closeSlot(Slot::Connect);
  closeSlot(Slot::Session);
}

void GlazewmSocket::run() {
  bool warned = false;
  while (!stopping_) {
    try {
      const bool ok = connectOnce();
      if (ok) {
        warned = false;
        void* webSocket = nullptr;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          webSocket = handles_.webSocket;
        }
        connected_ = true;
        emitConnected(true);
        if (webSocket) receiveLoop(webSocket);
        connected_ = false;
        emitConnected(false);
      } else if (!stopping_ && !warned) {
        qInfo() << "GlazeWM socket could not connect to" << url_ << "; retrying";
        warned = true;
      }
    } catch (const std::exception& e) {
      qWarning() << "GlazeWM socket worker error:" << e.what();
      if (connected_.exchange(false)) emitConnected(false);
    } catch (...) {
      qWarning() << "GlazeWM socket worker unknown error";
      if (connected_.exchange(false)) emitConnected(false);
    }
    closeAll();
    if (stopping_) break;
    if (!sleep(timing_.reconnectMs)) break;
  }
}

bool GlazewmSocket::connectOnce() {
  const QUrl url(url_);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || url.host().isEmpty() ||
      (scheme != QStringLiteral("ws") && scheme != QStringLiteral("wss"))) {
    qWarning() << "GlazeWM socket got an invalid URL:" << url_;
    return false;
  }
  const bool secure = scheme == QStringLiteral("wss");
  const INTERNET_PORT port = static_cast<INTERNET_PORT>(url.port(secure ? 443 : 80));
  QString target = url.path(QUrl::FullyEncoded);
  if (target.isEmpty()) target = QStringLiteral("/");
  if (url.hasQuery()) target += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
  const std::wstring host = url.host().toStdWString();
  const std::wstring path = target.toStdWString();

  HINTERNET session = WinHttpOpen(L"wabar", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  if (!publish(Slot::Session, session)) return false;
  const int t = timing_.connectTimeoutMs;
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

void GlazewmSocket::receiveLoop(void* webSocket) {
  std::vector<char> chunk(kChunkBytes);
  QByteArray message;
  bool binary = false;
  bool overflow = false;
  while (!stopping_) {
    DWORD read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
    const DWORD error = WinHttpWebSocketReceive(webSocket, chunk.data(), kChunkBytes, &read, &type);
    if (error == ERROR_WINHTTP_TIMEOUT) continue;
    if (error != ERROR_SUCCESS) {
      if (!stopping_) qWarning() << "GlazeWM socket receive failed:" << error;
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

}  // namespace wabar::workspaces::glazewm

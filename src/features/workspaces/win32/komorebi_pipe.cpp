#include <windows.h>
#include <tlhelp32.h>

#include <QDebug>
#include <QProcess>
#include <utility>
#include <vector>

#include "features/workspaces/komorebi.h"
#include "kernel/win32/shell_launch.h"

namespace wabar::workspaces::komorebi {
namespace {

const QString kKomorebic = QStringLiteral("komorebic.exe");

bool isProcessRunning(const wchar_t* exeName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found;
       ok = Process32NextW(snapshot, &entry)) {
    found = _wcsicmp(entry.szExeFile, exeName) == 0;
  }
  CloseHandle(snapshot);
  return found;
}

KomorebicResult runKomorebic(const QStringList& args, int timeoutMs) {
  KomorebicResult result;
  QProcess process;
  wabar::kernel::hideConsole(process);
  process.setProgram(kKomorebic);
  process.setArguments(args);
  process.start();
  if (!process.waitForStarted(timeoutMs)) {
    result.standardError = process.errorString().toUtf8();
    return result;
  }
  result.started = true;
  if (!process.waitForFinished(timeoutMs)) {
    result.timedOut = true;
    process.kill();
    process.waitForFinished(1000);
    return result;
  }
  result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
  result.standardOutput = process.readAllStandardOutput();
  result.standardError = process.readAllStandardError();
  return result;
}

// Collapses whitespace like Python's " ".join(s.split()).
QString simplifiedStderr(const QByteArray& stderrBytes) {
  return QString::fromUtf8(stderrBytes).simplified();
}

}  // namespace

KomorebiBackend defaultKomorebiBackend() {
  return {[] { return isProcessRunning(L"komorebi.exe"); }, &runKomorebic};
}

bool launchKomorebic(const QStringList& args) {
  const bool ok = wabar::kernel::startDetachedHidden(kKomorebic, args);
  if (!ok) qWarning().noquote() << "Failed to run komorebic" << args.join(QLatin1Char(' '));
  return ok;
}

std::optional<long long> monitorFromWindow(quintptr hwnd) {
  if (!hwnd) return std::nullopt;
  HMONITOR monitor = MonitorFromWindow(reinterpret_cast<HWND>(hwnd), MONITOR_DEFAULTTONULL);
  if (!monitor) return std::nullopt;
  return static_cast<long long>(reinterpret_cast<quintptr>(monitor));
}

KomorebiPipeListener::KomorebiPipeListener(KomorebiBackend backend, Callbacks callbacks,
                                           QString pipeName, Timing timing)
    : backend_(std::move(backend)),
      callbacks_(std::move(callbacks)),
      pipeName_(std::move(pipeName)),
      timing_(timing) {}

KomorebiPipeListener::~KomorebiPipeListener() { stop(); }

void KomorebiPipeListener::start() {
  if (thread_.joinable()) return;
  stopping_ = false;
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ioEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stopEvent_ || !ioEvent_) {
    qWarning() << "Komorebi listener could not create its events:" << GetLastError();
    if (stopEvent_) CloseHandle(stopEvent_);
    if (ioEvent_) CloseHandle(ioEvent_);
    stopEvent_ = ioEvent_ = nullptr;
    return;
  }
  thread_ = std::thread([this] { run(); });
}

void KomorebiPipeListener::stop() {
  if (!thread_.joinable()) return;
  stopping_ = true;
  SetEvent(stopEvent_);
  thread_.join();
  CloseHandle(stopEvent_);
  CloseHandle(ioEvent_);
  stopEvent_ = ioEvent_ = nullptr;
  if (subscribed_.exchange(false) && backend_.runKomorebic) {
    backend_.runKomorebic({QStringLiteral("unsubscribe-pipe"), pipeName_},
                          timing_.unsubscribeTimeoutMs);
  }
}

bool KomorebiPipeListener::sleep(int ms) {
  return WaitForSingleObject(stopEvent_, static_cast<DWORD>(ms)) == WAIT_TIMEOUT;
}

void KomorebiPipeListener::run() {
  const std::wstring fullName = L"\\\\.\\pipe\\" + pipeName_.toStdWString();
  while (!stopping_) {
    HANDLE pipe = CreateNamedPipeW(
        fullName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        kBufferSize, kBufferSize, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      qWarning().noquote() << "Komorebi listener could not create named pipe" << pipeName_
                           << "error" << GetLastError();
    } else {
      pipe_ = pipe;
      qInfo().noquote() << "Created named pipe" << pipeName_;
      if (waitUntilSubscribed() && connectPipe() && fetchState()) readLoop();
      CloseHandle(pipe);
      pipe_ = nullptr;
    }
    if (callbacks_.disconnected) callbacks_.disconnected();
    if (stopping_) break;
    qInfo() << "Attempting to reconnect to Komorebi...";
    if (!sleep(timing_.reconnectMs)) break;
  }
}

bool KomorebiPipeListener::waitUntilSubscribed() {
  bool warned = false;
  while (!stopping_) {
    QString error = QStringLiteral("Komorebi is not running, please start Komorebi.");
    if (backend_.isKomorebiRunning && backend_.isKomorebiRunning()) {
      const KomorebicResult result = backend_.runKomorebic(
          {QStringLiteral("subscribe-pipe"), pipeName_}, timing_.subscribeTimeoutMs);
      if (result.started && !result.timedOut && result.exitCode == 0) {
        subscribed_ = true;
        return true;
      }
      const QString stderrText = simplifiedStderr(result.standardError);
      if (!stderrText.contains(QStringLiteral("(os error 10061)"))) error = stderrText;
    }
    if (!warned) {
      qWarning().noquote() << "Komorebi failed to subscribe named pipe." << error;
      warned = true;
    }
    if (!sleep(timing_.subscribeRetryMs)) return false;
  }
  return false;
}

bool KomorebiPipeListener::connectPipe() {
  OVERLAPPED overlapped{};
  overlapped.hEvent = ioEvent_;
  ResetEvent(ioEvent_);
  if (!ConnectNamedPipe(pipe_, &overlapped)) {
    const DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
      HANDLE handles[2] = {ioEvent_, stopEvent_};
      const DWORD which = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      if (which != WAIT_OBJECT_0) {
        CancelIoEx(pipe_, &overlapped);
        DWORD ignored = 0;
        GetOverlappedResult(pipe_, &overlapped, &ignored, TRUE);
        return false;
      }
      DWORD ignored = 0;
      if (!GetOverlappedResult(pipe_, &overlapped, &ignored, FALSE)) {
        qWarning() << "Komorebi pipe connect failed:" << GetLastError();
        return false;
      }
    } else if (error != ERROR_PIPE_CONNECTED) {
      qWarning() << "Komorebi pipe connect failed:" << error;
      return false;
    }
  }
  if (stopping_) return false;
  qInfo().noquote() << "Komorebi connected to named pipe:" << pipeName_;
  return true;
}

bool KomorebiPipeListener::fetchState() {
  while (!stopping_) {
    const KomorebicResult result =
        backend_.runKomorebic({QStringLiteral("state")}, timing_.stateTimeoutMs);
    if (result.timedOut) {
      qWarning() << "Komorebi state query timed out in" << timing_.stateTimeoutMs << "ms";
    }
    const bool ok = result.started && !result.timedOut && result.exitCode == 0 &&
                    !result.standardOutput.trimmed().isEmpty();
    if (ok && callbacks_.connected && callbacks_.connected(result.standardOutput)) return true;
    qWarning() << "Failed to retrieve komorebi state before starting event listener. Retrying in"
               << timing_.stateRetryMs << "ms... Is komorebi online and komorebic on PATH?";
    if (!sleep(timing_.stateRetryMs)) return false;
  }
  return false;
}

void KomorebiPipeListener::readLoop() {
  std::vector<char> chunk(kBufferSize);
  QByteArray message;
  while (!stopping_) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = ioEvent_;
    ResetEvent(ioEvent_);
    DWORD read = 0;
    BOOL ok = ReadFile(pipe_, chunk.data(), kBufferSize, &read, &overlapped);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && error == ERROR_IO_PENDING) {
      HANDLE handles[2] = {ioEvent_, stopEvent_};
      const DWORD which = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      if (which != WAIT_OBJECT_0) {
        CancelIoEx(pipe_, &overlapped);
        GetOverlappedResult(pipe_, &overlapped, &read, TRUE);
        return;
      }
      ok = GetOverlappedResult(pipe_, &overlapped, &read, FALSE);
      error = ok ? ERROR_SUCCESS : GetLastError();
    } else if (ok || error == ERROR_MORE_DATA) {
      GetOverlappedResult(pipe_, &overlapped, &read, FALSE);
    }
    if (!ok && error != ERROR_MORE_DATA) {
      if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
        qWarning() << "Komorebi pipe has been ended:" << error;
      } else {
        qWarning() << "Unexpected komorebi pipe error:" << error;
      }
      return;
    }
    if (message.size() + static_cast<qsizetype>(read) > kMaxMessageBytes) {
      qWarning() << "Komorebi pipe message exceeds" << kMaxMessageBytes << "bytes; reconnecting";
      return;
    }
    message.append(chunk.data(), static_cast<qsizetype>(read));
    if (!ok) continue;  // ERROR_MORE_DATA: the rest of this message follows
    QByteArray complete;
    complete.swap(message);
    if (complete.trimmed().isEmpty()) continue;
    if (callbacks_.message) callbacks_.message(complete);
  }
}

}  // namespace wabar::workspaces::komorebi

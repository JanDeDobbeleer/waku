#include <windows.h>
#include <tlhelp32.h>

#include <QDebug>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "features/workspaces/komorebi.h"
#include "kernel/win32/shell_launch.h"

namespace wabar::workspaces::komorebi {
namespace {

const QString kKomorebic = QStringLiteral("komorebic.exe");
// How often a running komorebic's output pipes are drained while waiting for it.
constexpr DWORD kDrainSliceMs = 20;

// Closes a Win32 handle on scope exit.
struct OwnedHandle {
  HANDLE handle = nullptr;
  OwnedHandle() = default;
  OwnedHandle(const OwnedHandle&) = delete;
  OwnedHandle& operator=(const OwnedHandle&) = delete;
  ~OwnedHandle() { reset(); }
  void reset() {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = nullptr;
  }
};

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

// One argument quoted for CommandLineToArgvW (backslashes double only before a quote).
std::wstring quoteArgument(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    out += c;
    backslashes = 0;
  }
  out.append(backslashes * 2, L'\\');
  out += L'"';
  return out;
}

// Reads what is in the pipe now without blocking.
void drain(HANDLE pipe, QByteArray& out) {
  char buffer[4096];
  DWORD available = 0;
  while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
    DWORD read = 0;
    const DWORD want = std::min<DWORD>(available, sizeof(buffer));
    if (!ReadFile(pipe, buffer, want, &read, nullptr) || read == 0) return;
    out.append(buffer, static_cast<qsizetype>(read));
  }
}

// Runs komorebic.exe from PATH without a console. Plain Win32 instead of QProcess, which needs a
// QThread with an event dispatcher; this runs on the listener's std::thread. Both output pipes are
// drained while it runs, so a large `state` cannot fill a pipe and stall the child. `cancel` (an
// event, may be null) kills the child at once, so stop() never waits for a timeout.
KomorebicResult runKomorebic(const QStringList& args, int timeoutMs, void* cancel) {
  KomorebicResult result;
  SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  OwnedHandle outRead, outWrite, errRead, errWrite, input;
  if (!CreatePipe(&outRead.handle, &outWrite.handle, &inheritable, 0) ||
      !CreatePipe(&errRead.handle, &errWrite.handle, &inheritable, 0)) {
    result.standardError = QByteArrayLiteral("could not create the komorebic pipes");
    return result;
  }
  SetHandleInformation(outRead.handle, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(errRead.handle, HANDLE_FLAG_INHERIT, 0);
  input.handle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                             OPEN_EXISTING, 0, nullptr);
  if (input.handle == INVALID_HANDLE_VALUE) input.handle = nullptr;

  // Only these handles reach the child, not whatever else is inheritable at this moment.
  std::vector<HANDLE> inherit{outWrite.handle, errWrite.handle};
  if (input.handle) inherit.push_back(input.handle);
  SIZE_T attributeBytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
  std::vector<char> attributeBuffer(attributeBytes);
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
  if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
    result.standardError = QByteArrayLiteral("could not prepare the komorebic start");
    return result;
  }
  UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
                            inherit.size() * sizeof(HANDLE), nullptr, nullptr);

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input.handle;
  startup.StartupInfo.hStdOutput = outWrite.handle;
  startup.StartupInfo.hStdError = errWrite.handle;
  startup.lpAttributeList = attributes;

  std::wstring commandLine = kKomorebic.toStdWString();
  for (const QString& arg : args) commandLine += L' ' + quoteArgument(arg.toStdWString());
  PROCESS_INFORMATION info{};
  const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                      nullptr, &startup.StartupInfo, &info);
  const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
  DeleteProcThreadAttributeList(attributes);
  if (!created) {
    result.standardError =
        QStringLiteral("komorebic.exe could not start (error %1)").arg(createError).toUtf8();
    return result;
  }
  OwnedHandle process, thread;
  process.handle = info.hProcess;
  thread.handle = info.hThread;
  // The child holds its own copies; ours would keep the pipes open after it exits.
  outWrite.reset();
  errWrite.reset();
  input.reset();
  result.started = true;

  const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(std::max(timeoutMs, 0));
  HANDLE waits[2] = {process.handle, cancel};
  const DWORD waitCount = cancel ? 2 : 1;
  bool exited = false;
  for (;;) {
    drain(outRead.handle, result.standardOutput);
    drain(errRead.handle, result.standardError);
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) break;
    const DWORD slice = static_cast<DWORD>(std::min<ULONGLONG>(kDrainSliceMs, deadline - now));
    const DWORD which = WaitForMultipleObjects(waitCount, waits, FALSE, slice);
    if (which == WAIT_OBJECT_0) {
      exited = true;
      break;
    }
    if (which == WAIT_OBJECT_0 + 1) {
      result.cancelled = true;
      break;
    }
    if (which == WAIT_FAILED) break;
  }
  if (!exited) {
    result.timedOut = !result.cancelled;
    TerminateProcess(process.handle, 1);
    WaitForSingleObject(process.handle, 1000);
    return result;
  }
  drain(outRead.handle, result.standardOutput);
  drain(errRead.handle, result.standardError);
  DWORD code = 0;
  result.exitCode = GetExitCodeProcess(process.handle, &code) ? static_cast<int>(code) : -1;
  return result;
}

// Collapses whitespace like Python's " ".join(s.split()).
QString simplifiedStderr(const QByteArray& stderrBytes) {
  return QString::fromUtf8(stderrBytes).simplified();
}

}  // namespace

KomorebiBackend defaultKomorebiBackend() {
  return {[] { return isProcessRunning(L"komorebi.exe"); }, &runKomorebic, &launchKomorebic};
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
  // Fire and forget: stop() runs on the GUI thread during a reload and must not wait for it.
  if (subscribed_.exchange(false) && backend_.launchKomorebic) {
    backend_.launchKomorebic({QStringLiteral("unsubscribe-pipe"), pipeName_});
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
          {QStringLiteral("subscribe-pipe"), pipeName_}, timing_.subscribeTimeoutMs, stopEvent_);
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
        backend_.runKomorebic({QStringLiteral("state")}, timing_.stateTimeoutMs, stopEvent_);
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

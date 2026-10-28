#include "platform/systray_hook.h"

// windows.h first.
#include <windows.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <cwchar>
#include <vector>

#include "platform/systray_pipe.h"
#include "platform/systray_win.h"

namespace yasb::platform {
namespace {

HWND findShellTrayWnd(DWORD explorerPid) {
  struct Search {
    DWORD pid;
    HWND found;
  } search{explorerPid, nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        auto* state = reinterpret_cast<Search*>(param);
        wchar_t className[64] = {};
        if (!GetClassNameW(hwnd, className, 64) || wcscmp(className, L"Shell_TrayWnd") != 0)
          return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != state->pid) return TRUE;
        state->found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.found;
}

class ExplorerInjector final : public TrayInjector {
 public:
  explicit ExplorerInjector(QString dllPath) : dllPath_(std::move(dllPath)) {}

  std::optional<std::uint32_t> explorerPid() override { return platform::explorerPid(); }

  bool isHookLoaded(std::uint32_t pid) override {
    return isModuleLoaded(pid, QFileInfo(dllPath_).fileName());
  }

  // Injects dllPath_ into Explorer through a WH_GETMESSAGE hook on the Shell_TrayWnd thread. The
  // caller unhooks once the DLL's pipe has connected.
  std::uintptr_t inject(std::uint32_t pid) override {
    HMODULE dll = LoadLibraryW(reinterpret_cast<LPCWSTR>(dllPath_.utf16()));
    if (!dll) {
      qCritical() << "Tray hook: failed to load the DLL locally, error" << GetLastError();
      return 0;
    }
    std::uintptr_t result = 0;
    do {
      FARPROC proc = GetProcAddress(dll, "GetMsgProc");
      if (!proc) {
        qCritical() << "Tray hook: GetMsgProc not found in the DLL";
        break;
      }
      HWND tray = nullptr;
      for (int attempt = 0; attempt < 60 && !tray; ++attempt) {
        tray = findShellTrayWnd(pid);
        if (!tray) QThread::msleep(50);
      }
      if (!tray) {
        qCritical() << "Tray hook: Shell_TrayWnd not found";
        break;
      }
      const DWORD threadId = GetWindowThreadProcessId(tray, nullptr);
      if (!threadId) {
        qCritical() << "Tray hook: GetWindowThreadProcessId failed";
        break;
      }
      HHOOK hook =
          SetWindowsHookExW(WH_GETMESSAGE, reinterpret_cast<HOOKPROC>(proc), dll, threadId);
      if (!hook) {
        qCritical() << "Tray hook: SetWindowsHookExW failed, error" << GetLastError();
        break;
      }
      // WM_NULL makes Explorer's thread dequeue a message and load the DLL at once.
      PostThreadMessageW(threadId, WM_NULL, 0, 0);
      result = reinterpret_cast<std::uintptr_t>(hook);
    } while (false);
    FreeLibrary(dll);
    return result;
  }

  void release(std::uintptr_t hook) override {
    if (hook) UnhookWindowsHookEx(reinterpret_cast<HHOOK>(hook));
  }

 private:
  QString dllPath_;
};

HANDLE asHandle(void* value) { return static_cast<HANDLE>(value); }

}  // namespace

std::unique_ptr<TrayInjector> makeExplorerInjector(const QString& dllPath) {
  return std::make_unique<ExplorerInjector>(dllPath);
}

QString findTrayHookDll(const QString& appDir, QString* reason) {
  const auto fail = [&](const QString& why) {
    if (reason) *reason = why;
    return QString();
  };
  if (runningUnderEmulation()) return fail(QStringLiteral("running under emulation"));
  const QString name = trayHookDllName();
  if (name.isEmpty()) return fail(QStringLiteral("unsupported architecture"));
  for (const QString& folder : {appDir, appDir + QStringLiteral("/lib")}) {
    const QString candidate = QDir(folder).filePath(name);
    if (QFileInfo::exists(candidate)) return QDir::toNativeSeparators(candidate);
  }
  return fail(QStringLiteral("%1 not found next to the executable").arg(name));
}

struct SystrayHook::Io {
  void* pipe = nullptr;
  void* event = nullptr;
  std::vector<char> buffer;
  DWORD lastError = 0;
};

SystrayHook::SystrayHook(Options options, QObject* parent)
    : TrayHost(parent), options_(std::move(options)) {
  // The watchdog mutex is held for the whole life of the host; the DLL waits on it and detaches
  // itself when it is released or abandoned.
  mutex_ = CreateMutexW(nullptr, TRUE, reinterpret_cast<LPCWSTR>(options_.mutexName.utf16()));
  if (!mutex_) {
    qCritical() << "Tray hook: failed to create the watchdog mutex, error" << GetLastError();
    return;
  }
  pipe_ = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(options_.pipeName.utf16()),
                           PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
                           kHookPipeBufferSize, kHookPipeBufferSize, 0, nullptr);
  if (!pipe_ || pipe_ == INVALID_HANDLE_VALUE) {
    qCritical() << "Tray hook: failed to create the pipe, error" << GetLastError();
    pipe_ = nullptr;
    CloseHandle(asHandle(mutex_));
    mutex_ = nullptr;
  }
}

SystrayHook::~SystrayHook() { stop(); }

bool SystrayHook::valid() const { return mutex_ != nullptr && pipe_ != nullptr; }

void SystrayHook::start() {
  std::lock_guard lock(state_);
  if (thread_.joinable() || !pipe_ || stopRequested_) return;
  thread_ = std::thread([this] {
    SetThreadDescription(GetCurrentThread(), L"SystrayHook");
    run();
  });
}

void SystrayHook::unhookLocked() {
  if (hook_) {
    if (options_.injector) options_.injector->release(hook_);
    hook_ = 0;
  }
}

void SystrayHook::stop() {
  {
    std::lock_guard lock(state_);
    stopRequested_ = true;
    running_ = false;
    unhookLocked();
    if (pipe_) {
      if (workerActive_) {
        // The worker drains the cancellation before it releases its OVERLAPPED and buffer.
        CancelIoEx(asHandle(pipe_), nullptr);
      } else {
        CloseHandle(asHandle(pipe_));
        pipe_ = nullptr;
      }
    }
    if (mutex_) {
      // Releasing (on the creating thread) lets the DLL's watchdog wake and detach at once; if
      // another thread stops us the OS abandons the mutex when the process exits.
      ReleaseMutex(asHandle(mutex_));
      CloseHandle(asHandle(mutex_));
      mutex_ = nullptr;
    }
  }
  stopCv_.notify_all();
  if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

bool SystrayHook::waitStop(std::chrono::milliseconds delay) {
  if (options_.retryObserver) options_.retryObserver(delay);
  std::unique_lock lock(state_);
  return stopCv_.wait_for(lock, delay, [this] { return stopRequested_; });
}

void SystrayHook::run() {
  Io io;
  {
    std::lock_guard lock(state_);
    if (workerActive_ || !pipe_ || stopRequested_) return;
    io.pipe = pipe_;
    running_ = true;
    workerActive_ = true;
  }

  io.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!io.event) {
    qCritical() << "Tray hook: failed to create the pipe event, error" << GetLastError();
  } else {
    io.buffer.resize(kHookPipeBufferSize);
    // Retry after Explorer restarts or injection fails.
    while (running_) {
      if (!ensureInjected()) continue;
      runPipeSession(io);
      waitStop(options_.afterSessionDelay);
    }
  }

  {
    std::lock_guard lock(state_);
    running_ = false;
    unhookLocked();
    CloseHandle(asHandle(pipe_));
    pipe_ = nullptr;
    workerActive_ = false;
  }
  if (io.event) CloseHandle(asHandle(io.event));
}

bool SystrayHook::ensureInjected() {
  if (!options_.injector) {
    waitStop(kNoExplorerDelay);
    return false;
  }
  const std::optional<std::uint32_t> pid = options_.injector->explorerPid();
  if (!pid) {
    waitStop(kNoExplorerDelay);
    return false;
  }
  if (!running_) return false;
  if (options_.injector->isHookLoaded(*pid)) return running_;

  qInfo() << "Tray hook: injecting into Explorer, PID" << *pid;
  const std::uintptr_t hook = options_.injector->inject(*pid);
  if (!hook) {
    qCritical() << "Tray hook: injection failed, retrying in 5 s";
    waitStop(kInjectionFailedDelay);
    return false;
  }
  std::lock_guard lock(state_);
  // Injection can finish after stop() has already run.
  if (!running_) {
    options_.injector->release(hook);
    return false;
  }
  hook_ = hook;
  return true;
}

void SystrayHook::runPipeSession(Io& io) {
  const auto finish = [&] {
    // A failed connection must release its hook before another injection attempt.
    {
      std::lock_guard lock(state_);
      unhookLocked();
    }
    DisconnectNamedPipe(asHandle(io.pipe));
  };
  try {
    qDebug() << "Tray hook: waiting for the DLL to connect";
    if (!running_ || connectPipe(io) != Connect::Connected || !running_) {
      if (running_ && io.lastError && io.lastError != ERROR_BROKEN_PIPE &&
          io.lastError != ERROR_NO_DATA && io.lastError != ERROR_OPERATION_ABORTED) {
        qCritical() << "Tray hook: connect error" << io.lastError;
      }
      finish();
      return;
    }
    qDebug() << "Tray hook: DLL connected";
    {
      std::lock_guard lock(state_);
      unhookLocked();
    }
    emit updateIcons();

    while (running_) {
      QByteArray message;
      const Read status = readMessage(io, message);
      if (status != Read::Message || !running_) {
        if (status == Read::Broken && running_) {
          if (io.lastError == ERROR_BROKEN_PIPE) {
            qDebug() << "Tray hook: DLL disconnected";
          } else if (io.lastError != ERROR_OPERATION_ABORTED) {
            qCritical() << "Tray hook: read error" << io.lastError;
          }
        }
        break;
      }
      handleMessage(message);
    }
  } catch (const std::exception& error) {
    // A handler that throws must not unwind out of the worker thread.
    if (running_) qCritical() << "Tray hook: worker error:" << error.what();
  }
  finish();
}

SystrayHook::Connect SystrayHook::connectPipe(Io& io) {
  io.lastError = 0;
  OVERLAPPED overlapped{};
  overlapped.hEvent = asHandle(io.event);
  ResetEvent(overlapped.hEvent);
  if (ConnectNamedPipe(asHandle(io.pipe), &overlapped)) return Connect::Connected;
  const DWORD error = GetLastError();
  if (error == ERROR_PIPE_CONNECTED) return Connect::Connected;
  if (error != ERROR_IO_PENDING) {
    io.lastError = error;
    return Connect::Broken;
  }
  if (!waitForIo(io, &overlapped)) return Connect::Stopped;
  DWORD transferred = 0;
  if (!GetOverlappedResult(asHandle(io.pipe), &overlapped, &transferred, FALSE)) {
    io.lastError = GetLastError();
    return Connect::Broken;
  }
  return Connect::Connected;
}

SystrayHook::Read SystrayHook::readMessage(Io& io, QByteArray& message) {
  io.lastError = 0;
  message.clear();
  while (running_) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = asHandle(io.event);
    ResetEvent(overlapped.hEvent);
    const BOOL started = ReadFile(asHandle(io.pipe), io.buffer.data(),
                                  static_cast<DWORD>(io.buffer.size()), nullptr, &overlapped);
    DWORD error = started ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_IO_PENDING) {
      if (!waitForIo(io, &overlapped)) return Read::Stopped;
    } else if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) {
      io.lastError = error;
      return Read::Broken;
    }
    DWORD transferred = 0;
    const BOOL done = GetOverlappedResult(asHandle(io.pipe), &overlapped, &transferred, FALSE);
    error = done ? ERROR_SUCCESS : GetLastError();
    if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) {
      io.lastError = error;
      return Read::Broken;
    }
    message.append(io.buffer.data(), static_cast<qsizetype>(transferred));
    if (error == ERROR_SUCCESS) return Read::Message;
  }
  return Read::Stopped;
}

bool SystrayHook::waitForIo(Io& io, void* overlappedPtr) {
  auto* overlapped = static_cast<OVERLAPPED*>(overlappedPtr);
  bool completed = false;
  while (running_) {
    const DWORD result = WaitForSingleObject(overlapped->hEvent, 500);
    if (result == WAIT_OBJECT_0) {
      completed = true;
      break;
    }
    if (result == WAIT_FAILED) {
      io.lastError = GetLastError();
      break;
    }
  }
  if (!completed) {
    // Cancel and drain so the OVERLAPPED may go out of scope safely.
    CancelIoEx(asHandle(io.pipe), overlapped);
    DWORD transferred = 0;
    GetOverlappedResult(asHandle(io.pipe), overlapped, &transferred, TRUE);
  }
  return completed;
}

void SystrayHook::handleMessage(const QByteArray& message) {
  ParsedTrayMessage parsed = parseTrayPipeMessage(message, exePathFromHwnd);
  switch (parsed.kind) {
    case ParsedTrayMessage::Kind::Text:
      qDebug().noquote() << "Tray hook DLL:" << parsed.text.trimmed();
      break;
    case ParsedTrayMessage::Kind::IconModified:
      // The DLL could not read the icon (stale or unusual handle): try from this side.
      if ((parsed.data.uFlags & kNifIcon) && parsed.data.iconRgba.isEmpty())
        extractIconRgba(parsed.data);
      emit iconModified(parsed.data);
      break;
    case ParsedTrayMessage::Kind::IconDeleted:
      emit iconDeleted(parsed.data);
      break;
    case ParsedTrayMessage::Kind::Invalid:
      qWarning().noquote() << "Tray hook: dropped a malformed message:" << parsed.invalidReason;
      break;
    case ParsedTrayMessage::Kind::Ignored:
      break;
  }
}

}  // namespace yasb::platform

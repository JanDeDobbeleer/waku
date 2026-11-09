#include "features/tray/win32/tray_window_host.h"

// windows.h first.
#include <windows.h>

#include <QDebug>
#include <cwchar>
#include <future>
#include <vector>

#include "features/tray/icon_scale.h"
#include "features/tray/tray_pipe.h"
#include "features/tray/win32/tray_win.h"

namespace waku::tray {
namespace {

constexpr UINT_PTR kPollTimer = 1;
constexpr UINT_PTR kRecheckTimer = 2;
constexpr ULONG_PTR kTrayCopyData = 1;      // dwData of a Shell_NotifyIcon message
constexpr ULONG_PTR kAppBarCopyData = 0;    // SHAppBarMessage
constexpr ULONG_PTR kIconRectCopyData = 3;  // Shell_NotifyIconGetRect
constexpr DWORD kForwardTimeoutMs = 2000;
constexpr ULONGLONG kOwnBroadcastWindowMs = 2000;
constexpr UINT kRecheckDelayMs = 300;
constexpr ULONGLONG kRebroadcastDelayMs = 2500;
constexpr ULONGLONG kShellRebroadcastDelayMs = 1500;
constexpr int kPollsBeforeGivingUp = 2;

constexpr wchar_t kSecondaryClass[] = L"Shell_SecondaryTrayWnd";

LRESULT CALLBACK hostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* host = reinterpret_cast<TrayWindowHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!host) return DefWindowProcW(hwnd, message, wParam, lParam);
  return static_cast<LRESULT>(
      host->handle(hwnd, message, static_cast<std::uint64_t>(wParam), static_cast<LPARAM>(lParam)));
}

// The taskbar windows other than `self`: the first main one (for its rectangle and as the target
// of what is passed on) and whether any is visible.
struct Taskbars {
  const wchar_t* mainClass = nullptr;
  HWND self = nullptr;
  HWND main = nullptr;
  bool anyVisible = false;
};

Taskbars scanTaskbars(const std::wstring& mainClass, HWND self) {
  Taskbars result;
  result.mainClass = mainClass.c_str();
  result.self = self;
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        auto* state = reinterpret_cast<Taskbars*>(param);
        if (hwnd == state->self) return TRUE;
        wchar_t className[64] = {};
        if (!GetClassNameW(hwnd, className, 64)) return TRUE;
        const bool isMain = wcscmp(className, state->mainClass) == 0;
        if (!isMain && wcscmp(className, kSecondaryClass) != 0) return TRUE;
        if (isMain && !state->main) state->main = hwnd;
        if (IsWindowVisible(hwnd)) state->anyVisible = true;
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
  return result;
}

}  // namespace

TrayWindowHost::TrayWindowHost(Options options, QObject* parent)
    : TrayHost(parent), options_(std::move(options)) {
  mutex_ = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(options_.mutexName.utf16()));
  if (mutex_ && GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(static_cast<HANDLE>(mutex_));
    mutex_ = nullptr;
  }
}

TrayWindowHost::~TrayWindowHost() {
  stop();
  if (mutex_) CloseHandle(static_cast<HANDLE>(mutex_));
}

void TrayWindowHost::start() {
  if (!valid() || thread_.joinable()) return;
  std::promise<bool> ready;
  std::future<bool> done = ready.get_future();
  thread_ = std::thread([this, &ready] { run(&ready); });
  // The worker sets the value right after it has created the window (or failed to).
  if (done.wait_for(std::chrono::seconds(5)) != std::future_status::ready || !done.get()) {
    qWarning() << "Tray window host: could not create the window";
    stop();
  }
}

void TrayWindowHost::stop() {
  if (!thread_.joinable()) return;
  if (const auto window = window_.load()) {
    PostMessageW(reinterpret_cast<HWND>(window), WM_CLOSE, 0, 0);
  }
  thread_.join();
  window_ = 0;
  if (owning_.exchange(false) && options_.claimOrder) {
    // Explorer's tray is the one found again: let the programs send their icons to it.
    if (const UINT message = RegisterWindowMessageW(L"TaskbarCreated")) {
      SendNotifyMessageW(HWND_BROADCAST, message, 0, 0);
    }
  }
}

void TrayWindowHost::run(void* readyPointer) {
  auto* ready = static_cast<std::promise<bool>*>(readyPointer);
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = hostWindowProc;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = options_.windowClass.c_str();
  const bool registered = RegisterClassExW(&windowClass) != 0;
  if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    ready->set_value(false);
    return;
  }
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, options_.windowClass.c_str(),
                              L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    if (registered) UnregisterClassW(options_.windowClass.c_str(), instance);
    ready->set_value(false);
    return;
  }
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  // Elevated programs send from a higher integrity level; lower ones are refused by the system
  // unless the message is allowed. WM_COPYDATA is the only message that carries icon data.
  ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  window_ = reinterpret_cast<std::uint64_t>(hwnd);
  if (options_.claimOrder) {
    SetTimer(hwnd, kPollTimer, static_cast<UINT>(options_.pollInterval.count()), nullptr);
    poll(hwnd);
  }
  ready->set_value(true);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (registered) UnregisterClassW(options_.windowClass.c_str(), instance);
}

std::int64_t TrayWindowHost::handle(void* hwndPointer, unsigned message, std::uint64_t wParam,
                                    std::int64_t lParam) {
  HWND hwnd = static_cast<HWND>(hwndPointer);
  switch (message) {
    case WM_COPYDATA:
      return onCopyData(hwnd, wParam, lParam);
    case WM_TIMER:
      if (wParam == kRecheckTimer) KillTimer(hwnd, kRecheckTimer);
      if (wParam == kPollTimer || wParam == kRecheckTimer) poll(hwnd);
      return 0;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  if (taskbarCreated_ && message == taskbarCreated_) {
    // Explorer made a new taskbar (a restart): look again soon, once its window can be hidden.
    if (GetTickCount64() >= ownBroadcastUntil_ && options_.claimOrder) {
      SetTimer(hwnd, kRecheckTimer, kRecheckDelayMs, nullptr);
      // The programs now send their icons to whichever window is found first at that moment,
      // Explorer's new one perhaps: once this one is first again, tell them once more.
      rebroadcastAt_ = GetTickCount64() + kShellRebroadcastDelayMs;
    }
    return 0;
  }
  // Nothing else is passed on (see the class comment).
  return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
}

std::int64_t TrayWindowHost::onCopyData(void* hwnd, std::uint64_t wParam, std::int64_t lParam) {
  const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
  if (!data) return FALSE;
  // dwData 1 is a tray icon message. 0 is the app bar protocol, 3 the icon rectangle query: not
  // ours, Explorer answers them. Any other value is nobody's business here.
  if (data->dwData == kAppBarCopyData || data->dwData == kIconRectCopyData) {
    return forward(hwnd, WM_COPYDATA, wParam, lParam);
  }
  if (data->dwData != kTrayCopyData) return FALSE;
  if (!data->lpData || data->cbData < static_cast<DWORD>(kShellTrayDataSize)) return FALSE;

  ParsedTrayMessage parsed =
      parseShellTrayData(static_cast<const char*>(data->lpData), data->cbData, nullptr, 0, 0, 0,
                         [](std::uint64_t window) { return exePathFromHwnd(window); });
  switch (parsed.kind) {
    case ParsedTrayMessage::Kind::IconModified:
      if (parsed.data.uFlags & kNifIcon) {
        extractIconRgba(parsed.data);
        parsed.data.scaledRgba = scaledTrayIconRgba(parsed.data);
      }
      qDebug().noquote() << "Tray window host: message" << parsed.data.messageType << "from"
                         << parsed.data.exe << "uID" << parsed.data.uID << "flags" << Qt::hex
                         << parsed.data.uFlags << "icon"
                         << (parsed.data.iconRgba.isEmpty() ? parsed.data.iconError
                                                            : QStringLiteral("ok"));
      {
        std::lock_guard lock(iconsMutex_);
        icons_.modified(parsed.data);
      }
      emit iconModified(parsed.data);
      break;
    case ParsedTrayMessage::Kind::IconDeleted:
      qDebug().noquote() << "Tray window host: delete uID" << parsed.data.uID << "window" << Qt::hex
                         << parsed.data.hWnd;
      {
        std::lock_guard lock(iconsMutex_);
        icons_.deleted(parsed.data);
      }
      emit iconDeleted(parsed.data);
      break;
    case ParsedTrayMessage::Kind::Invalid:
      qWarning().noquote() << "Tray window host: malformed message:" << parsed.invalidReason;
      return FALSE;
    default:
      break;
  }
  return TRUE;
}

std::int64_t TrayWindowHost::forward(void* hwndPointer, unsigned message, std::uint64_t wParam,
                                     std::int64_t lParam) {
  HWND hwnd = static_cast<HWND>(hwndPointer);
  if (!options_.forwardToExplorer) {
    return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
  }
  HWND target = static_cast<HWND>(explorerTaskbar(hwnd));
  if (!target) return 0;
  if (InSendMessage()) {
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(target, message, static_cast<WPARAM>(wParam),
                             static_cast<LPARAM>(lParam), SMTO_ABORTIFHUNG, kForwardTimeoutMs,
                             &result)) {
      return 0;
    }
    return static_cast<std::int64_t>(result);
  }
  PostMessageW(target, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
  return 0;
}

void* TrayWindowHost::explorerTaskbar(void* selfPointer) {
  HWND self = static_cast<HWND>(selfPointer);
  const auto ownedByExplorer = [this](HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid != 0 && pid == explorerPid_;
  };
  HWND cached = static_cast<HWND>(explorerWnd_);
  if (cached && cached != self && IsWindow(cached) && ownedByExplorer(cached)) return cached;
  explorerWnd_ = nullptr;
  // Whatever else has this window class (another tray host, a test) is not who answers.
  const auto pid = explorerPid();
  if (!pid) return nullptr;
  explorerPid_ = *pid;
  struct Search {
    const wchar_t* className;
    HWND self;
    DWORD pid;
    HWND found;
  } search{options_.windowClass.c_str(), self, explorerPid_, nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        auto* state = reinterpret_cast<Search*>(param);
        if (hwnd == state->self) return TRUE;
        wchar_t className[64] = {};
        if (!GetClassNameW(hwnd, className, 64) || wcscmp(className, state->className) != 0) {
          return TRUE;
        }
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);
        if (windowPid != state->pid) return TRUE;
        state->found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&search));
  explorerWnd_ = search.found;
  return search.found;
}

void TrayWindowHost::replay() {
  std::vector<TrayIconData> known;
  {
    std::lock_guard lock(iconsMutex_);
    std::vector<TrayIconData> gone;
    for (const int id : icons_.ids()) {
      const TrayIconData* data = icons_.data(id);
      if (!data) continue;
      // An owner that went away without a NIM_DELETE is dropped here, not announced.
      if (IsWindow(reinterpret_cast<HWND>(data->hWnd))) {
        known.push_back(*data);
      } else {
        gone.push_back(*data);
      }
    }
    for (const TrayIconData& data : gone) icons_.deleted(data);
  }
  for (TrayIconData& data : known) {
    // The accumulated record as one modification that names every field it carries.
    data.messageType = kNimModify;
    data.uFlags = kNifMessage | kNifIcon | kNifTip | kNifState | (data.guid ? kNifGuid : 0);
    emit iconModified(data);
  }
}

void TrayWindowHost::poll(void* hwndPointer) {
  if (!options_.claimOrder) return;
  HWND hwnd = static_cast<HWND>(hwndPointer);
  const Taskbars bars = scanTaskbars(options_.windowClass, hwnd);
  const bool claim = !bars.anyVisible;
  const bool wasOwning = owning_.load();
  if (claim) {
    visiblePolls_ = 0;
    const auto found = [&] { return FindWindowW(options_.windowClass.c_str(), nullptr); };
    if (!wasOwning || found() != hwnd) {
      if (wasOwning) qDebug() << "Tray window host: another window is found first, raising";
      RECT rect{0, 0, 1, 1};
      if (bars.main) GetWindowRect(bars.main, &rect);
      // Hidden, but at the taskbar's place, so a program that asks for the taskbar's rectangle gets
      // a sensible one.
      SetWindowPos(hwnd, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                   rect.bottom - rect.top, SWP_NOACTIVATE);
    }
    // Owning means found: the programs get TaskbarCreated only once this window is first.
    if (!wasOwning) {
      if (found() != hwnd) {
        qDebug() << "Tray window host: not found first yet, trying again";
        return;
      }
      owning_ = true;
      qInfo() << "Tray window host: owns Shell_TrayWnd (the Windows taskbar is hidden)";
      broadcastTaskbarCreated();
      // Once more after the first answers: a program that was busy misses the first broadcast.
      rebroadcastAt_ = GetTickCount64() + kRebroadcastDelayMs;
    } else if (rebroadcastAt_ && GetTickCount64() >= rebroadcastAt_) {
      rebroadcastAt_ = 0;
      if (found() == hwnd) broadcastTaskbarCreated();
    }
  } else if (wasOwning) {
    // An Explorer restart shows its new taskbar for a moment before it is hidden again: give the
    // role up only when the window stays visible.
    if (++visiblePolls_ < kPollsBeforeGivingUp) return;
    visiblePolls_ = 0;
    rebroadcastAt_ = 0;
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    owning_ = false;
    qInfo() << "Tray window host: gave up Shell_TrayWnd (the Windows taskbar is visible)";
    broadcastTaskbarCreated();
  }
}

void TrayWindowHost::broadcastTaskbarCreated() {
  if (!taskbarCreated_) return;
  ownBroadcastUntil_ = GetTickCount64() + kOwnBroadcastWindowMs;
  SendNotifyMessageW(HWND_BROADCAST, taskbarCreated_, 0, 0);
}

}  // namespace waku::tray

#include <Windows.h>

#include <QDebug>
#include <QLoggingCategory>
#include <atomic>
#include <future>
#include <thread>
#include <utility>

#include "features/taskbar/progress_cache.h"

namespace waku::taskbar {
namespace {

// Each message the receiver gets and each bind, for `QT_LOGGING_RULES=waku.taskbar.progress.debug=
// true` with `--log-file`: it tells "the application sent nothing" from "waku dropped it".
Q_LOGGING_CATEGORY(lcProgress, "waku.taskbar.progress", QtInfoMsg)

// ITaskbarList3's private wire protocol, verified on Windows 11 build 26300.9818.
constexpr UINT kProgressValue = WM_USER + 64;
constexpr UINT kProgressState = WM_USER + 65;
constexpr double kProgressScale = 65534.0;
constexpr wchar_t kProgressClass[] = L"WakuTaskbarProgress";
constexpr wchar_t kTaskbandProperty[] = L"TaskbandHWND";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 1000;
// Sent by the owner to stop: the worker unbinds, destroys the window and leaves its loop.
constexpr UINT kStop = WM_APP + 1;

std::uint32_t ownerProcess(HWND hwnd) {
  DWORD process = 0;
  GetWindowThreadProcessId(hwnd, &process);
  return process;
}

QString className(HWND hwnd) {
  wchar_t name[128] = {};
  const int length = GetClassNameW(hwnd, name, 128);
  return QString::fromWCharArray(name, length > 0 ? length : 0);
}

// Threads. One worker thread owns the receiver window, its timer and its message loop, from
// construction to destruction. Applications send to it (ITaskbarList3 sends synchronously), so a
// busy GUI thread during a shell reload never blocks them. The worker writes the cache; the cache
// signals the GUI thread queued. The destructor posts kStop and joins.
class Win32ProgressReceiver final : public ProgressReceiver {
 public:
  explicit Win32ProgressReceiver(std::shared_ptr<ProgressCache> cache) : cache_(std::move(cache)) {}
  ~Win32ProgressReceiver() override {
    if (const HWND window = window_) PostMessageW(window, kStop, 0, 0);
    if (thread_.joinable()) thread_.join();
  }

  bool start() {
    std::promise<bool> ready;
    std::future<bool> created = ready.get_future();
    thread_ = std::thread([this, &ready] { run(ready); });
    if (created.get()) return true;
    thread_.join();
    return false;
  }

 private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Win32ProgressReceiver*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
      case kProgressValue:
      case kProgressState:
        self->receive(message, wparam, lparam);
        return 0;
      case WM_TIMER:
        if (wparam == kTimerId) {
          self->bind();
          self->prune();
          return 0;
        }
        break;
      case kStop:
        self->unbind();
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  void run(std::promise<bool>& ready) {
    WNDCLASSW definition{};
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpfnWndProc = &Win32ProgressReceiver::windowProc;
    definition.lpszClassName = kProgressClass;
    HWND window = nullptr;
    if (RegisterClassW(&definition) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
      window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kProgressClass, L"", WS_POPUP,
                               0, 0, 1, 1, nullptr, nullptr, definition.hInstance, this);
    }
    if (!window) {
      qWarning() << "Taskbar progress receiver unavailable:" << GetLastError();
      ready.set_value(false);
      return;
    }
    // A sender with a higher integrity level (an elevated terminal) reaches it too.
    ChangeWindowMessageFilterEx(window, kProgressValue, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, kProgressState, MSGFLT_ALLOW, nullptr);
    window_ = window;
    bind();
    SetTimer(window, kTimerId, kTimerMs, nullptr);
    ready.set_value(true);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    window_ = nullptr;
  }

  void receive(UINT message, WPARAM wparam, LPARAM lparam) {
    const auto target = reinterpret_cast<HWND>(wparam);
    // Both messages carry integers only. Never dereference a sender's data.
    const std::uint32_t pid = IsWindow(target) ? ownerProcess(target) : 0;
    qCDebug(lcProgress).nospace() << (message == kProgressValue ? "value" : "state") << " hwnd=0x"
                                  << Qt::hex << wparam << Qt::dec << " lparam=" << lparam
                                  << " pid=" << pid << " class=" << className(target) << " root=0x"
                                  << Qt::hex
                                  << reinterpret_cast<quintptr>(GetAncestor(target, GA_ROOTOWNER));
    if (pid == 0) return;
    const auto hwnd = static_cast<WindowHandle>(wparam);
    if (message == kProgressValue && lparam >= 0 && lparam <= 65534)
      cache_->setValue(hwnd, pid, static_cast<double>(lparam) / kProgressScale);
    else if (message == kProgressState)
      cache_->setState(hwnd, pid, static_cast<int>(lparam));
  }

  // Points the TaskbandHWND property of our own tray host at the receiver. Only a Shell_TrayWnd of
  // this process: Explorer's or another shell's is never touched. The tray host can come later or
  // be made again, so the timer checks every second.
  void bind() {
    const HWND window = window_;
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"Shell_TrayWnd", nullptr))) {
      if (ownerProcess(tray) != GetCurrentProcessId()) continue;
      if (tray == tray_ && GetPropW(tray, kTaskbandProperty) == window) return;
      previous_ = static_cast<HWND>(GetPropW(tray, kTaskbandProperty));
      if (SetPropW(tray, kTaskbandProperty, window)) {
        tray_ = tray;
        qCDebug(lcProgress).nospace()
            << "bound to tray 0x" << Qt::hex << reinterpret_cast<quintptr>(tray);
      }
      return;
    }
  }

  void unbind() {
    if (tray_ && IsWindow(tray_) && GetPropW(tray_, kTaskbandProperty) == window_.load()) {
      if (previous_ && IsWindow(previous_))
        SetPropW(tray_, kTaskbandProperty, previous_);
      else
        RemovePropW(tray_, kTaskbandProperty);
    }
    tray_ = nullptr;
    previous_ = nullptr;
  }

  // A window that is gone, or now belongs to another process, loses its progress.
  void prune() {
    cache_->prune([](WindowHandle hwnd, std::uint32_t pid) {
      const auto window = reinterpret_cast<HWND>(hwnd);
      return IsWindow(window) && ownerProcess(window) == pid;
    });
  }

  std::shared_ptr<ProgressCache> cache_;
  std::thread thread_;
  std::atomic<HWND> window_ = nullptr;
  // Worker thread only.
  HWND tray_ = nullptr;
  HWND previous_ = nullptr;
};

}  // namespace

std::unique_ptr<ProgressReceiver> makeWin32ProgressReceiver(std::shared_ptr<ProgressCache> cache) {
  if (!cache) return nullptr;
  auto receiver = std::make_unique<Win32ProgressReceiver>(std::move(cache));
  if (!receiver->start()) return nullptr;
  return receiver;
}

}  // namespace waku::taskbar

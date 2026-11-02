#include <windows.h>
#include <shellapi.h>

#include <QString>
#include <vector>

#include "host/windows_taskbar.h"

namespace waku::host {

namespace {

// Documented APIs only: SHAppBarMessage(ABM_GETSTATE / ABM_SETSTATE), EnumWindows on the
// documented class names, ShowWindow, RegisterWindowMessage("TaskbarCreated").
constexpr wchar_t kPrimaryClass[] = L"Shell_TrayWnd";
constexpr wchar_t kSecondaryClass[] = L"Shell_SecondaryTrayWnd";
constexpr wchar_t kWatcherClass[] = L"WakuTaskbarWatcher";
constexpr UINT_PTR kRecheckTimer = 1;

std::vector<HWND> taskbarWindows() {
  std::vector<HWND> found;
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        wchar_t name[64] = {};
        if (GetClassNameW(hwnd, name, 64) > 0 &&
            (wcscmp(name, kPrimaryClass) == 0 || wcscmp(name, kSecondaryClass) == 0)) {
          reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&found));
  return found;
}

class Win32TaskbarSystem final : public TaskbarSystem {
 public:
  Win32TaskbarSystem() : restartMessage_(RegisterWindowMessageW(L"TaskbarCreated")) {}
  ~Win32TaskbarSystem() override {
    if (watcher_) DestroyWindow(watcher_);
  }

  bool autoHide() const override { return (state() & ABS_AUTOHIDE) != 0; }

  void setAutoHide(bool on) override {
    UINT value = state();
    value = on ? (value | ABS_AUTOHIDE) : (value & ~static_cast<UINT>(ABS_AUTOHIDE));
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = FindWindowW(kPrimaryClass, nullptr);
    data.lParam = static_cast<LPARAM>(value);
    SHAppBarMessage(ABM_SETSTATE, &data);
    if (on) waitForFullWorkArea();
  }

  bool windowsVisible() const override {
    for (HWND hwnd : taskbarWindows()) {
      if (IsWindowVisible(hwnd)) return true;
    }
    return false;
  }

  void setWindowsHidden(bool hidden) override {
    for (HWND hwnd : taskbarWindows()) ShowWindow(hwnd, hidden ? SW_HIDE : SW_SHOWNA);
  }

  bool processAlive(qint64 pid) const override {
    if (pid <= 0) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    if (alive) {
      // A reused pid is not a waku: compare the image name.
      wchar_t path[MAX_PATH] = {};
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        alive = QString::fromWCharArray(path, static_cast<qsizetype>(size))
                    .endsWith(QStringLiteral("waku.exe"), Qt::CaseInsensitive);
      }
    }
    CloseHandle(process);
    return alive;
  }

  void onExplorerRestart(std::function<void()> callback) override {
    callback_ = std::move(callback);
    if (callback_ && !watcher_) createWatcher();
    if (!callback_ && watcher_) {
      DestroyWindow(watcher_);
      watcher_ = nullptr;
    }
  }

 private:
  UINT state() const {
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = FindWindowW(kPrimaryClass, nullptr);
    return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &data));
  }

  // Explorer applies ABM_SETSTATE on its own thread: wait (at most 0.5 s) until the primary
  // monitor's work area is the whole monitor, so an app bar registered next does not see the old
  // work area. Another app bar on the monitor keeps it smaller, which ends the wait on time.
  static void waitForFullWorkArea() {
    for (int i = 0; i < 25; ++i) {
      MONITORINFO info{};
      info.cbSize = sizeof(info);
      if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info)) return;
      if (EqualRect(&info.rcWork, &info.rcMonitor)) return;
      Sleep(20);
    }
  }

  // A hidden top-level window, not a message-only one: message-only windows do not receive the
  // broadcast TaskbarCreated. It never shows. The GUI thread's message loop delivers to it.
  void createWatcher() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Win32TaskbarSystem::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWatcherClass;
    RegisterClassW(&wc);  // fails harmlessly when already registered
    watcher_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWatcherClass, L"", WS_POPUP, 0,
                               0, 0, 0, nullptr, nullptr, wc.hInstance, this);
    // Explorer shows an auto-hide taskbar again when the work area changes (an app bar is
    // registered, a monitor is added): a once-a-second check puts the hidden windows back.
    if (watcher_) SetTimer(watcher_, kRecheckTimer, 1000, nullptr);
  }

  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (auto* self =
                   reinterpret_cast<Win32TaskbarSystem*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
               self && self->callback_ &&
               (message == self->restartMessage_ ||
                (message == WM_TIMER && wparam == kRecheckTimer))) {
      try {
        self->callback_();  // exceptions never cross the window procedure
      } catch (...) {
      }
      return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  UINT restartMessage_;
  HWND watcher_ = nullptr;
  std::function<void()> callback_;
};

}  // namespace

std::unique_ptr<TaskbarSystem> makeWin32TaskbarSystem() {
  return std::make_unique<Win32TaskbarSystem>();
}

}  // namespace waku::host

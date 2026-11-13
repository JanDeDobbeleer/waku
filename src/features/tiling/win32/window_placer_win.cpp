// The real WindowPlacer. Documented APIs only: EnumWindows, GetWindowLong, GetWindowRect,
// DwmGetWindowAttribute (DWMWA_EXTENDED_FRAME_BOUNDS, DWMWA_CLOAKED), MonitorFromWindow,
// GetMonitorInfo, IVirtualDesktopManager::GetWindowDesktopId, OpenProcessToken and
// SetWindowPos. The shown desktop comes from workspaces' vdesk::currentDesktopId() (its registry
// value is on the allowlist in docs/architecture.md).
//
// place() never activates a window and never blocks on a hung one: SWP_ASYNCWINDOWPOS posts the
// request, so the readback it returns can be stale; the engine verifies the bounds later.
// SetWindowPos is called here and nowhere else in the tiler.
#include <Windows.h>
#include <dwmapi.h>

#include <vector>

#include "features/tiling/layouts.h"
#include "features/tiling/window_placer.h"
#include "features/workspaces/virtual_desktops.h"
#include "kernel/win32/window_focus.h"

namespace waku::tiling {
namespace {

HWND toHwnd(WindowId id) { return reinterpret_cast<HWND>(id); }
QRect toRect(const RECT& r) { return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top); }

QString windowTitle(HWND hwnd) {
  wchar_t buffer[512] = {};
  const int copied = GetWindowTextW(hwnd, buffer, 512);
  return QString::fromWCharArray(buffer, copied);
}

QString windowClass(HWND hwnd) {
  wchar_t buffer[256] = {};
  const int copied = GetClassNameW(hwnd, buffer, 256);
  return QString::fromWCharArray(buffer, copied);
}

bool isCloaked(HWND hwnd) {
  DWORD cloaked = 0;
  return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
         cloaked != 0;
}

QRect visibleBounds(HWND hwnd, const RECT& fallback) {
  RECT frame = {};
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
    return toRect(frame);
  }
  return toRect(fallback);
}

bool tokenElevated(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return true;  // protected: not movable
  TOKEN_ELEVATION elevation = {};
  DWORD size = 0;
  const bool ok =
      GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != 0;
  CloseHandle(token);
  return ok ? elevation.TokenIsElevated != 0 : true;
}

class Win32WindowPlacer final : public WindowPlacer {
 public:
  std::vector<WindowId> candidates() override {
    std::vector<WindowId> out;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
          if (IsWindowVisible(hwnd)) {
            reinterpret_cast<std::vector<WindowId>*>(param)->push_back(
                reinterpret_cast<WindowId>(hwnd));
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
  }

  // The cheap part: window state and place. No process handle, no title, no COM.
  std::optional<WindowFacts> state(WindowId id) override {
    HWND hwnd = toHwnd(id);
    if (!IsWindow(hwnd)) return std::nullopt;
    WindowFacts facts;
    facts.hwnd = id;
    facts.visible = IsWindowVisible(hwnd) != 0;
    facts.iconic = IsIconic(hwnd) != 0;
    facts.zoomed = IsZoomed(hwnd) != 0;
    facts.cloaked = isCloaked(hwnd);
    RECT window = {};
    GetWindowRect(hwnd, &window);
    facts.rect = visibleBounds(hwnd, window);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    facts.monitor = reinterpret_cast<MonitorId>(monitor);
    return facts;
  }

  // Everything classify() needs, once per window that is new to the tiler.
  std::optional<WindowFacts> query(WindowId id) override {
    std::optional<WindowFacts> found = state(id);
    if (!found) return std::nullopt;
    WindowFacts& facts = *found;
    HWND hwnd = toHwnd(id);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    facts.toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
    facts.thickFrame = (style & WS_THICKFRAME) != 0;
    facts.caption = (style & WS_CAPTION) == WS_CAPTION;
    HWND owner = GetWindow(hwnd, GW_OWNER);
    facts.ownedByVisible = owner != nullptr && IsWindowVisible(owner);
    facts.className = windowClass(hwnd);
    facts.title = windowTitle(hwnd);

    RECT window = {};
    GetWindowRect(hwnd, &window);
    HMONITOR monitor = reinterpret_cast<HMONITOR>(facts.monitor);
    MONITORINFO info = {sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info)) {
      facts.fullscreen = !facts.iconic && window.left <= info.rcMonitor.left &&
                         window.top <= info.rcMonitor.top && window.right >= info.rcMonitor.right &&
                         window.bottom >= info.rcMonitor.bottom;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    facts.ownProcess = pid == GetCurrentProcessId();
    if (pid && !facts.ownProcess) {
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (process) {
        wchar_t path[MAX_PATH] = {};
        DWORD length = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &length)) {
          const QString full = QString::fromWCharArray(path, static_cast<qsizetype>(length));
          facts.processName = full.mid(full.lastIndexOf(QLatin1Char('\\')) + 1);
        }
        facts.elevated = tokenElevated(process);
        CloseHandle(process);
      } else {
        facts.elevated = true;  // cannot even be opened: not ours to move
      }
    }
    facts.desktopId =
        workspaces::vdesk::windowDesktopId(static_cast<quint64>(id)).value_or(QString());
    return facts;
  }

  QRect workArea(MonitorId monitor) override {
    MONITORINFO info = {sizeof(info)};
    if (!monitor || !GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor), &info)) return {};
    return toRect(info.rcWork);
  }

  QString currentDesktop() override {
    return workspaces::vdesk::currentDesktopId().value_or(QString());
  }

  std::optional<WindowId> foreground() override {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return std::nullopt;
    return reinterpret_cast<WindowId>(hwnd);
  }

  PlaceResult place(WindowId id, const QRect& slot) override {
    HWND hwnd = toHwnd(id);
    if (!IsWindow(hwnd)) return {};
    RECT window = {};
    if (!GetWindowRect(hwnd, &window)) return {};
    // The slot is the visible bounds; the window also owns an invisible border on each side.
    const QRect outer = outerRect(slot, toRect(window), visibleBounds(hwnd, window));
    const UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_NOCOPYBITS |
                       SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS;
    if (!SetWindowPos(hwnd, nullptr, outer.x(), outer.y(), outer.width(), outer.height(), flags)) {
      return {};
    }
    RECT after = {};
    GetWindowRect(hwnd, &after);
    return {true, visibleBounds(hwnd, after)};
  }

  void activate(WindowId id) override { kernel::forceForegroundFocus(id); }

  // A documented z-order change only: HWND_TOP, no move, no size, no activation.
  void raise(WindowId id) override {
    SetWindowPos(toHwnd(id), HWND_TOP, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_ASYNCWINDOWPOS);
  }

  bool restore(WindowId id) override {
    return ShowWindowAsync(toHwnd(id), SW_SHOWNOACTIVATE) != FALSE;
  }

  // PostMessage and ShowWindowAsync do not wait for the window, so a hung one cannot block us.
  bool close(WindowId id) override {
    return PostMessageW(reinterpret_cast<HWND>(id), WM_CLOSE, 0, 0) != FALSE;
  }
  bool minimize(WindowId id) override {
    return ShowWindowAsync(reinterpret_cast<HWND>(id), SW_MINIMIZE) != FALSE;
  }
};

}  // namespace

std::unique_ptr<WindowPlacer> makeWin32WindowPlacer() {
  return std::make_unique<Win32WindowPlacer>();
}

bool processIsElevated() { return tokenElevated(GetCurrentProcess()); }

}  // namespace waku::tiling

// The real VirtualDesktopSystem. The desktop list comes from the registry values Explorer keeps
// under HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops (undocumented; the
// allowlist row is in docs/architecture.md). The window count per desktop uses the documented
// IVirtualDesktopManager. A jump to a desktop moves a small off-screen window of this process
// there with IVirtualDesktopManager::MoveWindowToDesktop and activates it, which makes Windows
// switch (verified on build 26300; a WS_EX_TOOLWINDOW or layered window is not tracked by the
// desktop service and does not work). Creating a desktop sends Win+Ctrl+D with SendInput, the
// documented keyboard shortcut. No undocumented COM interface is involved.
#include <Windows.h>
#include <ShObjIdl.h>

#include <QByteArray>
#include <QDebug>
#include <cstring>
#include <string>
#include <vector>

#include "features/workspaces/virtual_desktops.h"
#include "kernel/win32/com_scope.h"

namespace waku::workspaces::vdesk {
namespace {

constexpr wchar_t kKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";

std::optional<QByteArray> readBinary(HKEY key, const wchar_t* name) {
  DWORD type = 0;
  DWORD size = 0;
  if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
      type != REG_BINARY || size == 0 || size > 64 * 1024) {
    return std::nullopt;
  }
  QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
  if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(bytes.data()), &size) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  bytes.resize(static_cast<qsizetype>(size));
  return bytes;
}

QString readDesktopName(const QString& id) {
  const std::wstring path = std::wstring(kKey) + L"\\Desktops\\" + id.toStdWString();
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t buffer[256] = {};
  DWORD size = sizeof(buffer) - sizeof(wchar_t);
  DWORD type = 0;
  QString name;
  if (RegQueryValueExW(key, L"Name", nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) ==
          ERROR_SUCCESS &&
      type == REG_SZ) {
    name = QString::fromWCharArray(buffer);
  }
  RegCloseKey(key);
  return name;
}

// A window the user would call "a window on this desktop": top-level, titled, not a tool window
// or an owned popup, not the shell's own.
bool countsAsWindow(HWND hwnd) {
  if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
    // Windows on other desktops are cloaked, not hidden, so they stay visible.
    return false;
  }
  if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
  const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if ((ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0) return false;
  if (GetWindowTextLengthW(hwnd) == 0) return false;
  wchar_t cls[64] = {};
  GetClassNameW(hwnd, cls, 64);
  return wcscmp(cls, L"Progman") != 0 && wcscmp(cls, L"WorkerW") != 0 &&
         wcscmp(cls, L"Shell_TrayWnd") != 0 && wcscmp(cls, L"Shell_SecondaryTrayWnd") != 0;
}

constexpr wchar_t kAnchorClass[] = L"WakuVirtualDesktopAnchor";
constexpr UINT_PTR kHideTimer = 1;
constexpr UINT kHideAfterMs = 700;  // the switch animation has ended

LRESULT CALLBACK anchorProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_TIMER && wParam == kHideTimer) {
    KillTimer(hwnd, kHideTimer);
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

class Win32VirtualDesktopSystem final : public VirtualDesktopSystem {
 public:
  ~Win32VirtualDesktopSystem() override {
    if (anchor_) DestroyWindow(anchor_);
    if (manager_) manager_->Release();
  }

  bool jumpTo(int index) override {
    IVirtualDesktopManager* mgr = manager();
    if (!mgr || index < 0) return failed("no IVirtualDesktopManager");
    const std::vector<GUID> ids = desktopGuids();
    if (index >= static_cast<int>(ids.size())) return failed("no such desktop");
    if (!anchor_ && !createAnchor()) return failed("the anchor window was not created");
    const GUID& target = ids[static_cast<size_t>(index)];
    if (FAILED(mgr->MoveWindowToDesktop(anchor_, target))) return failed("MoveWindowToDesktop");
    // The desktop service tracks a window only once it is shown, so the move is not verified:
    // it is made while the window is hidden and takes effect when the window appears.
    ShowWindow(anchor_, SW_SHOWNA);
    // A click on the bar does not always give the foreground right; SwitchToThisWindow is the call
    // Alt+Tab uses and has no such restriction.
    if (!SetForegroundWindow(anchor_)) SwitchToThisWindow(anchor_, TRUE);
    // The window stays until the animation is over, then goes away (and with it any entry the
    // taskbar list made for it).
    SetTimer(anchor_, kHideTimer, kHideAfterMs, nullptr);
    return true;
  }

  std::optional<State> read() override {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
      return std::nullopt;
    }
    const std::optional<QByteArray> all = readBinary(key, L"VirtualDesktopIDs");
    const std::optional<QByteArray> current = readBinary(key, L"CurrentVirtualDesktop");
    RegCloseKey(key);
    // Explorer has not written the list yet on a profile that never opened Task View: one
    // desktop, and the shown one is that desktop.
    const QStringList ids = all ? parseDesktopIds(*all) : QStringList();
    const QStringList shown = current ? parseDesktopIds(*current) : QStringList();
    if (ids.isEmpty()) return std::nullopt;

    State state;
    for (const QString& id : ids) {
      Desktop desktop;
      desktop.id = id;
      desktop.name = readDesktopName(id);
      state.desktops.push_back(std::move(desktop));
    }
    if (!shown.isEmpty()) {
      const qsizetype at = ids.indexOf(shown.first());
      if (at >= 0) state.current = static_cast<int>(at);
    }
    countWindows(state);
    return state;
  }

  void create() override {
    std::vector<INPUT> input;
    auto add = [&input](WORD vk, bool up) {
      INPUT in = {};
      in.type = INPUT_KEYBOARD;
      in.ki.wVk = vk;
      in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
      input.push_back(in);
    };
    add(VK_LWIN, false);
    add(VK_LCONTROL, false);
    add('D', false);
    add('D', true);
    add(VK_LCONTROL, true);
    add(VK_LWIN, true);
    SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT));
  }

 private:
  static bool failed(const char* reason) {
    qWarning().noquote() << "virtual desktops: jump failed:" << reason;
    return false;
  }

  static std::vector<GUID> desktopGuids() {
    std::vector<GUID> guids;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
      return guids;
    }
    const std::optional<QByteArray> all = readBinary(key, L"VirtualDesktopIDs");
    RegCloseKey(key);
    if (!all) return guids;
    for (qsizetype offset = 0; offset + 16 <= all->size(); offset += 16) {
      GUID guid;
      memcpy(&guid, all->constData() + offset, sizeof(guid));
      guids.push_back(guid);
    }
    return guids;
  }

  bool createAnchor() {
    static const bool registered = [] {
      WNDCLASSW wc = {};
      wc.lpfnWndProc = anchorProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = kAnchorClass;
      return RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered) return false;
    // A plain popup, off screen: the desktop service ignores tool and layered windows.
    anchor_ = CreateWindowExW(0, kAnchorClass, L"waku desktop anchor", WS_POPUP, -32000, -32000, 1,
                              1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return anchor_ != nullptr;
  }

  struct Counting {
    IVirtualDesktopManager* manager;
    State* state;
  };

  IVirtualDesktopManager* manager() {
    if (!manager_ && !failed_) {
      const kernel::ComScope com(kernel::ComApartment::SingleThreaded);
      if (FAILED(CoCreateInstance(__uuidof(VirtualDesktopManager), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&manager_)))) {
        manager_ = nullptr;
        failed_ = true;
      }
    }
    return manager_;
  }

  void countWindows(State& state) {
    IVirtualDesktopManager* mgr = manager();
    if (!mgr) return;
    Counting counting{mgr, &state};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
          auto* c = reinterpret_cast<Counting*>(param);
          if (!countsAsWindow(hwnd)) return TRUE;
          GUID id = {};
          if (FAILED(c->manager->GetWindowDesktopId(hwnd, &id))) return TRUE;
          wchar_t text[64] = {};
          if (StringFromGUID2(id, text, 64) == 0) return TRUE;
          const QString key = QString::fromWCharArray(text).toUpper();
          for (Desktop& desktop : c->state->desktops) {
            if (desktop.id == key) {
              ++desktop.windowCount;
              break;
            }
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&counting));
  }

  IVirtualDesktopManager* manager_ = nullptr;
  bool failed_ = false;
  HWND anchor_ = nullptr;
};

}  // namespace

std::unique_ptr<VirtualDesktopSystem> makeWin32VirtualDesktopSystem() {
  return std::make_unique<Win32VirtualDesktopSystem>();
}

}  // namespace waku::workspaces::vdesk

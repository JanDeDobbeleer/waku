// The real VirtualDesktopSystem. The desktop list comes from the registry values Explorer keeps
// under HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops (undocumented; the
// allowlist row is in docs/architecture.md). The windows per desktop come from the documented
// IVirtualDesktopManager. A jump to a desktop moves a small off-screen window of this process
// there with IVirtualDesktopManager::MoveWindowToDesktop and activates it, which makes Windows
// switch (verified on build 26300; a WS_EX_TOOLWINDOW or layered window is not tracked by the
// desktop service and does not work). Creating a desktop sends Win+Ctrl+D with SendInput, the
// documented keyboard shortcut. Where the shell interface behind Task View is open
// (shell_desktops_win.h), a switch, a create and a remove go through it instead; the anchor window
// and the keystroke stay as the fallback for switch and create, and remove has none.
#include <Windows.h>
#include <ShObjIdl.h>

#include <QByteArray>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "features/workspaces/virtual_desktops.h"
#include "features/workspaces/win32/shell_desktops_win.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/com_scope.h"

namespace waku::workspaces::vdesk {
namespace {

QString readDesktopName(const QString& id) {
  const std::wstring path = std::wstring(kVirtualDesktopsKey) + L"\\Desktops\\" + id.toStdWString();
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
// or an owned popup, not the shell's own and not one of this process (the jump anchor).
bool countsAsWindow(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId()) return false;
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
    if (ShellDesktopApi* api = shellApi()) {
      QStringList ids = registryIds();
      // A desktop that was just created may not be in the registry yet; the shell knows it.
      if (index >= ids.size()) ids = api->desktopIds().value_or(ids);
      const bool known = index >= 0 && index < ids.size();
      if (known && api->switchTo(ids.at(index), animate_)) return true;
      checkLost();
      // Falls through to the anchor window: the shell refused or is not there.
    }
    return jumpWithAnchor(index);
  }

  std::optional<State> read() override {
    // Explorer restarted: the next read opens the interface again (throttled).
    if (hook_ && !api_ && apiState_ == ApiState::Lost) shellApi();
    return readRegistry();
  }

  int create() override {
    if (ShellDesktopApi* api = shellApi()) {
      const std::optional<QString> id = api->create();
      if (id) {
        const std::optional<QStringList> ids = api->desktopIds();
        checkLost();
        // The desktop exists but is not shown; the caller shows it.
        return ids ? static_cast<int>(ids->indexOf(*id)) : -1;
      }
      checkLost();
    }
    sendCreateKeys();  // Windows shows the new desktop
    return -1;
  }

  bool canRemove() const override { return apiState_ == ApiState::Open; }

  bool remove(int index) override {
    ShellDesktopApi* api = shellApi();
    if (!api) return false;
    const QStringList ids = registryIds();
    const int fallback = removalFallbackIndex(static_cast<int>(ids.size()), index);
    if (fallback < 0) return false;
    qDebug().noquote() << "virtual desktops: closing desktop" << index << ids.at(index)
                       << "windows go to" << fallback << ids.at(fallback);
    const bool ok = api->remove(ids.at(index), ids.at(fallback));
    checkLost();
    return ok;
  }

  bool rename(int index, const QString& name) override {
    ShellDesktopApi* api = shellApi();
    const QStringList ids = registryIds();
    if (!api || index < 0 || index >= ids.size()) return false;
    const bool ok = api->rename(ids.at(index), name);
    checkLost();
    return ok;
  }

  void setAnimate(bool animate) override { animate_ = animate; }

  bool setChangedHook(std::function<void()> hook) override {
    hook_ = std::move(hook);
    bool active = false;
    if (!watcher_) {
      watcher_ = std::make_unique<RegistryChangeWatcher>();
      if (!watcher_->start([this] { fire(); })) watcher_.reset();
    }
    if (watcher_) active = true;
    if (shellApi() && registerNotifications()) active = true;
    return active;
  }

  bool jumpWithAnchor(int index) {
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

  std::optional<State> readRegistry() {
    const std::optional<QByteArray> all = readVirtualDesktopsValue(L"VirtualDesktopIDs");
    const std::optional<QByteArray> current = readVirtualDesktopsValue(L"CurrentVirtualDesktop");
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
    lastIds_ = ids;
    collectWindows(state);
    return state;
  }

  void sendCreateKeys() {
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
  enum class ApiState { Untried, Open, Lost, Unavailable };

  static QStringList registryIds() {
    const std::optional<QByteArray> all = readVirtualDesktopsValue(L"VirtualDesktopIDs");
    return all ? parseDesktopIds(*all) : QStringList();
  }

  void fire() {
    if (hook_) hook_();
  }

  // The shell interface, opened on first use. A failed first open is final for the run (logged
  // once); after Explorer went away it is retried at most every 10 s.
  ShellDesktopApi* shellApi() {
    if (api_) return api_.get();
    if (apiState_ == ApiState::Unavailable ||
        !kernel::liveDesktop(QStringLiteral("shell_desktops"))) {
      return nullptr;
    }
    const ULONGLONG now = GetTickCount64();
    if (apiState_ == ApiState::Lost && now < nextOpenAt_) return nullptr;
    QString reason;
    api_ = ShellDesktopApi::open(&reason);
    if (!api_) {
      if (apiState_ == ApiState::Untried) {
        qInfo().noquote() << "virtual desktops: shell api unavailable:" << reason;
        apiState_ = ApiState::Unavailable;
      } else {
        qDebug().noquote() << "virtual desktops: shell api still unavailable:" << reason;
        nextOpenAt_ = now + 10000;
      }
      return nullptr;
    }
    qInfo() << "virtual desktops: shell api open";
    apiState_ = ApiState::Open;
    if (hook_) registerNotifications();
    return api_.get();
  }

  // Drops the interface after a call showed Explorer is gone; shellApi() reopens it later.
  void checkLost() {
    if (!api_ || !api_->lost()) return;
    qWarning() << "virtual desktops: shell api lost (Explorer restarted?); it reopens later";
    api_.reset();
    apiState_ = ApiState::Lost;
    nextOpenAt_ = GetTickCount64() + 2000;
  }

  bool registerNotifications() {
    return api_ && api_->registerNotifications(
                       [this] { fire(); },
                       [this](const QString& destroyed, const QString& fallback) {
                         // What Task View used for its close button, for comparison with
                         // removalFallbackIndex().
                         qDebug().noquote()
                             << "virtual desktops: DestroyBegin" << destroyed << "index"
                             << lastIds_.indexOf(destroyed) << "windows go to" << fallback
                             << "index" << lastIds_.indexOf(fallback);
                       });
  }

  static bool failed(const char* reason) {
    qWarning().noquote() << "virtual desktops: jump failed:" << reason;
    return false;
  }

  static std::vector<GUID> desktopGuids() {
    std::vector<GUID> guids;
    const std::optional<QByteArray> all = readVirtualDesktopsValue(L"VirtualDesktopIDs");
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

  struct Collecting {
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

  // Puts each eligible window on its desktop. A window that belongs to no listed desktop (a
  // pinned "show on all desktops" window may report GUID_NULL or an id outside the list) goes to
  // the shown desktop when Windows says it is on the current one.
  void collectWindows(State& state) {
    IVirtualDesktopManager* mgr = manager();
    if (!mgr) return;
    Collecting collecting{mgr, &state};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
          auto* c = reinterpret_cast<Collecting*>(param);
          if (!countsAsWindow(hwnd)) return TRUE;
          Desktop* owner = nullptr;
          GUID id = {};
          wchar_t text[64] = {};
          if (SUCCEEDED(c->manager->GetWindowDesktopId(hwnd, &id)) &&
              StringFromGUID2(id, text, 64) != 0) {
            const QString key = QString::fromWCharArray(text).toUpper();
            for (Desktop& desktop : c->state->desktops) {
              if (desktop.id == key) {
                owner = &desktop;
                break;
              }
            }
          }
          if (!owner) {
            BOOL here = FALSE;
            const int current = c->state->current;
            if (current >= 0 &&
                SUCCEEDED(c->manager->IsWindowOnCurrentVirtualDesktop(hwnd, &here)) && here) {
              owner = &c->state->desktops[static_cast<size_t>(current)];
            }
          }
          if (owner) {
            owner->windows.push_back(static_cast<quint64>(reinterpret_cast<quintptr>(hwnd)));
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&collecting));
    // EnumWindows runs in z-order, which changes with every focus change. Sort, or every focus
    // change would look like a change of the desktop.
    for (Desktop& desktop : state.desktops) {
      std::sort(desktop.windows.begin(), desktop.windows.end());
    }
  }

  IVirtualDesktopManager* manager_ = nullptr;
  bool failed_ = false;
  HWND anchor_ = nullptr;
  std::unique_ptr<ShellDesktopApi> api_;
  ApiState apiState_ = ApiState::Untried;
  ULONGLONG nextOpenAt_ = 0;
  bool animate_ = true;
  std::function<void()> hook_;
  std::unique_ptr<RegistryChangeWatcher> watcher_;
  QStringList lastIds_;  // the registry list of the last read, for the DestroyBegin log
};

}  // namespace

std::optional<QString> currentDesktopId() {
  const std::optional<QByteArray> current = readVirtualDesktopsValue(L"CurrentVirtualDesktop");
  if (!current) return std::nullopt;
  const QStringList ids = parseDesktopIds(*current);
  if (ids.isEmpty()) return std::nullopt;
  return ids.first();
}

std::unique_ptr<VirtualDesktopSystem> makeWin32VirtualDesktopSystem() {
  return std::make_unique<Win32VirtualDesktopSystem>();
}

}  // namespace waku::workspaces::vdesk

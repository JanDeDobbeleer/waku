#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <QDebug>
#include <string>
#include <thread>

#include "features/control_center/touch_keyboard.h"

namespace wabar::control_center {
namespace {

// Undocumented TabTip COM server (docs/architecture.md allowlist).
struct __declspec(uuid("37C994E7-432B-4834-A2F7-DCE1F13B834B")) ITipInvocation : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
};

// {4CE576FA-83DC-4F88-951C-9D0782B4E376}
const CLSID kClsidUIHostNoLaunch = {
    0x4CE576FA, 0x83DC, 0x4F88, {0x95, 0x1C, 0x9D, 0x07, 0x82, 0xB4, 0xE3, 0x76}};

// An STA for the scope of one call; balanced with the CoInitializeEx that succeeded (S_FALSE
// included). A thread that is already MTA (RPC_E_CHANGED_MODE) stays as it is.
class ComScope {
 public:
  ComScope() : initialised_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
  ~ComScope() {
    if (initialised_) CoUninitialize();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

 private:
  bool initialised_;
};

// True when the TabTip server was reachable, even if Toggle itself failed (as in Python).
bool tryToggleViaCom() {
  ITipInvocation* tip = nullptr;
  const HRESULT hr =
      CoCreateInstance(kClsidUIHostNoLaunch, nullptr, CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                       __uuidof(ITipInvocation), reinterpret_cast<void**>(&tip));
  if (FAILED(hr) || !tip) return false;
  tip->Toggle(GetDesktopWindow());
  tip->Release();
  return true;
}

std::wstring tabTipPath() {
  wchar_t folder[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES_COMMON, nullptr, 0, folder))) return {};
  return std::wstring(folder) + L"\\microsoft shared\\ink\\TabTip.exe";
}

// Starts TabTip, waits until its message pump runs and toggles it.
void launchAndToggle() {
  const ComScope com;
  const std::wstring path = tabTipPath();
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  info.lpFile = path.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!path.empty() && ShellExecuteExW(&info) && info.hProcess) {
    WaitForInputIdle(info.hProcess, 5000);
    CloseHandle(info.hProcess);
    tryToggleViaCom();
  } else {
    qWarning() << "Failed to toggle touch keyboard: TabTip could not be started";
  }
}

}  // namespace

void toggleTouchKeyboard() {
  {
    const ComScope com;
    if (tryToggleViaCom()) return;
  }
  // TabTip is not running. Starting it and waiting for its message pump (up to 5 s) runs on a
  // thread of its own, not on the caller's (the GUI thread).
  //
  // Threads: one detached thread per launch; it owns nothing of ours, initialises COM for itself
  // and ends after the toggle or the 5 s wait.
  std::thread(launchAndToggle).detach();
}

}  // namespace wabar::control_center

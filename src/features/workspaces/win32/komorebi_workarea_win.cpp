// The real WorkAreaNotifier. Documented APIs only (FindWindowW, SendMessageTimeoutW,
// WM_SETTINGCHANGE, SPI_SETWORKAREA), so no row in the allowlist of docs/architecture.md. The
// class name `komorebi-hidden` is komorebi's (design.md, "Work area"); when komorebi does not run
// or renames it, nothing is found and nothing is sent.
#include <Windows.h>

#include "features/workspaces/komorebi_workarea.h"

namespace waku::workspaces::komorebi {
namespace {

class Win32WorkAreaNotifier final : public WorkAreaNotifier {
 public:
  WorkAreaNotifyResult notify() override {
    WorkAreaNotifyResult result;
    // Matched by class alone: komorebi creates exactly one window of this class.
    HWND window = FindWindowW(L"komorebi-hidden", nullptr);
    if (!window) return result;
    result.windowFound = true;
    result.hwnd = reinterpret_cast<quintptr>(window);
    // WM_SETTINGCHANGE carries a string pointer in lParam by contract, so Windows refuses to queue
    // it to another process (SendNotifyMessageW and PostMessageW fail with
    // ERROR_MESSAGE_SYNC_ONLY, 1159). Only a synchronous send works. The short timeout and
    // SMTO_ABORTIFHUNG bound the time a stuck komorebi can hold the caller; komorebi reads only
    // wParam and lParam is 0.
    DWORD_PTR ignored = 0;
    if (SendMessageTimeoutW(window, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 250, &ignored)) {
      result.sent = true;
    } else {
      result.error = GetLastError();
    }
    return result;
  }
};

}  // namespace

std::unique_ptr<WorkAreaNotifier> makeWin32WorkAreaNotifier() {
  return std::make_unique<Win32WorkAreaNotifier>();
}

}  // namespace waku::workspaces::komorebi

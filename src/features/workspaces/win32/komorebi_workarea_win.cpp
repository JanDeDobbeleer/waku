// The real WorkAreaNotifier. Documented APIs only (FindWindowW, SendNotifyMessageW,
// WM_SETTINGCHANGE, SPI_SETWORKAREA), so no row in the allowlist of docs/architecture.md. The
// class name `komorebi-hidden` is komorebi's (design.md, "Work area"); when komorebi does not run
// or renames it, nothing is found and nothing is sent.
#include <Windows.h>

#include "features/workspaces/komorebi_workarea.h"

namespace wabar::workspaces::komorebi {
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
    // Another thread's window: SendNotifyMessageW queues the message and returns at once, so a
    // hung komorebi never blocks the GUI thread. komorebi reads only wParam.
    if (SendNotifyMessageW(window, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0)) {
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

}  // namespace wabar::workspaces::komorebi

#include "kernel/win32/winevent_hook.h"

#include <windows.h>

#include <map>
#include <utility>

namespace waku::kernel {
namespace {

// The WinEvent callback is a bare function pointer that receives the hook handle, so the handle
// finds the callback. GUI thread only, like every install and uninstall.
std::map<std::uintptr_t, WinEventHook::Callback>& callbacks() {
  static std::map<std::uintptr_t, WinEventHook::Callback> table;
  return table;
}

void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                           DWORD, DWORD) {
  try {
    const auto found = callbacks().find(reinterpret_cast<std::uintptr_t>(hook));
    if (found == callbacks().end()) return;
    // A copy: the callback may uninstall this hook.
    const WinEventHook::Callback callback = found->second;
    if (callback) callback(event, reinterpret_cast<std::uintptr_t>(hwnd), idObject, idChild);
  } catch (...) {
    // Never unwind into the system.
  }
}

}  // namespace

WinEventHook::~WinEventHook() { uninstall(); }

WinEventHook::WinEventHook(WinEventHook&& other) noexcept
    : handle_(std::exchange(other.handle_, 0)) {}

WinEventHook& WinEventHook::operator=(WinEventHook&& other) noexcept {
  if (this != &other) {
    uninstall();
    handle_ = std::exchange(other.handle_, 0);
  }
  return *this;
}

bool WinEventHook::install(unsigned low, unsigned high, Callback callback, bool skipOwnProcess) {
  uninstall();
  const DWORD flags = WINEVENT_OUTOFCONTEXT | (skipOwnProcess ? WINEVENT_SKIPOWNPROCESS : 0);
  HWINEVENTHOOK hook = SetWinEventHook(low, high, nullptr, winEventProc, 0, 0, flags);
  if (!hook) return false;
  handle_ = reinterpret_cast<std::uintptr_t>(hook);
  callbacks()[handle_] = std::move(callback);
  return true;
}

void WinEventHook::uninstall() {
  if (!handle_) return;
  callbacks().erase(handle_);
  UnhookWinEvent(reinterpret_cast<HWINEVENTHOOK>(handle_));
  handle_ = 0;
}

}  // namespace waku::kernel

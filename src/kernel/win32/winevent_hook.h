#pragma once

#include <cstdint>
#include <functional>

namespace waku::kernel {

// One out-of-context WinEvent hook (SetWinEventHook), owned for its lifetime: the destructor
// unhooks. The callback runs on the thread that installed the hook, from its message loop (the GUI
// thread), never inside the process that raised the event. Events of this process are skipped
// (WINEVENT_SKIPOWNPROCESS) unless asked for.
//
// The callback never throws into the system: an exception that escapes it is swallowed. It gets
// the raw event code, the window handle and the object and child ids, unfiltered; filtering
// (idObject == OBJID_WINDOW, idChild == 0) is the caller's, because the right filter differs.
//
// Threading: GUI thread only. Install, move and destroy on the same thread. Not copyable.
class WinEventHook {
 public:
  using Callback =
      std::function<void(unsigned event, std::uintptr_t hwnd, long idObject, long idChild)>;

  WinEventHook() = default;
  ~WinEventHook();
  WinEventHook(const WinEventHook&) = delete;
  WinEventHook& operator=(const WinEventHook&) = delete;
  WinEventHook(WinEventHook&& other) noexcept;
  WinEventHook& operator=(WinEventHook&& other) noexcept;

  // Hooks the event codes `low`..`high`. Replaces a hook already installed. False when Windows
  // refused the hook (the object stays empty).
  bool install(unsigned low, unsigned high, Callback callback, bool skipOwnProcess = true);
  void uninstall();
  bool installed() const { return handle_ != 0; }

 private:
  std::uintptr_t handle_ = 0;
};

}  // namespace waku::kernel

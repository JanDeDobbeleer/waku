#pragma once

#include <cstdint>

namespace waku::kernel {

// Port of YASB's force_foreground_focus: brings the window `hwnd` to the foreground and gives it
// keyboard focus. Windows refuses SetForegroundWindow from a process that did not receive the last
// input event, so the call attaches this thread's input queue to the foreground thread's (the
// documented AttachThreadInput workaround) for the moment of the call. A caller in another process
// (`wakuc`) first calls AllowSetForegroundWindow for the bar's pid. Failures are ignored; a null
// `hwnd` does nothing. Call it on the thread that owns the window.
void forceForegroundFocus(std::uintptr_t hwnd);

}  // namespace waku::kernel

#pragma once

#include <QRect>
#include <cstdint>
#include <optional>

namespace wabar::wallpapers {

// Port of window_actions.force_foreground_focus: brings `hwnd` to the foreground by attaching to
// the foreground thread's input queue. Failures are ignored.
void forceForegroundFocus(std::uintptr_t hwnd);

// Rectangle (physical pixels) of the monitor holding the foreground window, if there is one.
std::optional<QRect> foregroundMonitorRect();

}  // namespace wabar::wallpapers

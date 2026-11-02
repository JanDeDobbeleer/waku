#pragma once

#include <QRect>
#include <optional>

namespace waku::wallpapers {

// Rectangle (physical pixels) of the monitor holding the foreground window, if there is one.
// Forcing the foreground to a window is kernel::forceForegroundFocus (kernel/win32/window_focus.h).
std::optional<QRect> foregroundMonitorRect();

}  // namespace waku::wallpapers

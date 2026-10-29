#pragma once

namespace wabar::control_center {

// Toggles the Windows 11 touch / on-screen keyboard (TabTip), as TouchKeyboardService.toggle:
// a direct COM toggle first; when TabTip is not running, launches it, waits for its message pump
// and toggles. Logs and returns on failure.
void toggleTouchKeyboard();

}  // namespace wabar::control_center

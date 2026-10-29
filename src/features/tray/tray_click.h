#pragma once

#include <cstdint>

#include "features/tray/tray_host.h"
#include "features/tray/tray_types.h"

namespace wabar::tray {

// winuser.h mouse messages, spelled out so no caller needs Win32 headers.
inline constexpr std::uint32_t kWmLButtonDown = 0x0201;
inline constexpr std::uint32_t kWmLButtonUp = 0x0202;
inline constexpr std::uint32_t kWmLButtonDblClk = 0x0203;
inline constexpr std::uint32_t kWmRButtonDown = 0x0204;
inline constexpr std::uint32_t kWmRButtonUp = 0x0205;
inline constexpr std::uint32_t kWmMButtonDown = 0x0207;
inline constexpr std::uint32_t kWmMButtonUp = 0x0208;

// True for the mouse messages above: the ones that let the owner take the foreground.
bool isTrayMouseMessage(std::uint32_t message);

// One notification to the owner of `icon` (port of IconWidget.send_notify): the callback message
// the icon registered, with the arguments its version expects.
//   version <= 3: wParam = the icon id, lParam = the event
//   version >= 4: wParam = the cursor position packed as two 16-bit values,
//                 lParam = the event and the icon id packed the same way
// Nothing is sent when the icon registered no callback message.
void sendTrayNotify(const TrayIconData& icon, TrayWindowApi& api, std::uint32_t message);

// A mouse action on a tray icon (port of IconWidget.send_action), the way the shell sends it:
// nothing when the owner window is gone; for a mouse message the owner is first allowed to take
// the foreground; then the message itself, and for version 3 and up a left button release is
// followed by NIN_SELECT and a right button release by NIN_CONTEXTMENU.
void sendTrayAction(const TrayIconData& icon, TrayWindowApi& api, std::uint32_t action);

}  // namespace wabar::tray

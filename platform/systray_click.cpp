#include "platform/systray_click.h"

#include "platform/systray_pipe.h"

namespace yasb::platform {

bool isTrayMouseMessage(std::uint32_t message) {
  switch (message) {
    case kWmLButtonDown:
    case kWmLButtonUp:
    case kWmLButtonDblClk:
    case kWmRButtonDown:
    case kWmRButtonUp:
    case kWmMButtonDown:
    case kWmMButtonUp:
      return true;
    default:
      return false;
  }
}

void sendTrayNotify(const TrayIconData& icon, TrayWindowApi& api, std::uint32_t message) {
  if (icon.uCallbackMessage == 0) return;
  std::uint64_t wParam = 0;
  std::int32_t lParam = 0;
  if (icon.uVersion > 3) {
    const auto [x, y] = api.cursorPos();
    wParam = static_cast<std::uint32_t>(packI32(x, y));
    lParam = packI32(static_cast<std::int32_t>(message), static_cast<std::int32_t>(icon.uID));
  } else {
    wParam = icon.uID;
    lParam = packI32(static_cast<std::int32_t>(message), 0);
  }
  api.sendNotifyMessage(icon.hWnd, icon.uCallbackMessage, wParam, lParam);
}

void sendTrayAction(const TrayIconData& icon, TrayWindowApi& api, std::uint32_t action) {
  if (!api.isWindow(icon.hWnd)) return;
  if (isTrayMouseMessage(action)) api.allowSetForeground(icon.hWnd);
  sendTrayNotify(icon, api, action);

  if (icon.uVersion >= 3) {
    if (action == kWmLButtonUp)
      sendTrayNotify(icon, api, kNinSelect);
    else if (action == kWmRButtonUp)
      sendTrayNotify(icon, api, kNinContextMenu);
  }
}

}  // namespace yasb::platform

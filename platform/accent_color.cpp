#include "platform/accent_color.h"

#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/base.h>

#include "platform/winrt_apartment.h"

namespace yasb::platform {

std::optional<std::uint32_t> systemAccentRgb() {
  try {
    ensureWinrtApartment();
    const winrt::Windows::UI::ViewManagement::UISettings settings;
    const auto color =
        settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
    return (static_cast<std::uint32_t>(color.R) << 16) |
           (static_cast<std::uint32_t>(color.G) << 8) | static_cast<std::uint32_t>(color.B);
  } catch (const winrt::hresult_error&) {
    return std::nullopt;
  }
}

}  // namespace yasb::platform

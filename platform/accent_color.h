#pragma once

#include <cstdint>
#include <optional>

namespace yasb::platform {

// The Windows accent colour (Windows.UI.ViewManagement.UISettings, UIColorType.Accent) as
// 0x00RRGGBB; nullopt when WinRT cannot supply it. May be called from any thread.
std::optional<std::uint32_t> systemAccentRgb();

// UIColorType.AccentLight2 / AccentDark1 as 0x00RRGGBB (the shades Python's theme.py maps onto the
// dark and light token sets); nullopt when WinRT cannot supply them.
enum class AccentShade { Light2, Dark1 };
std::optional<std::uint32_t> systemAccentShadeRgb(AccentShade shade);

}  // namespace yasb::platform

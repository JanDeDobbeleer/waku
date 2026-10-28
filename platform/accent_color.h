#pragma once

#include <cstdint>
#include <optional>

namespace yasb::platform {

// The Windows accent colour (Windows.UI.ViewManagement.UISettings, UIColorType.Accent) as
// 0x00RRGGBB; nullopt when WinRT cannot supply it. May be called from any thread.
std::optional<std::uint32_t> systemAccentRgb();

}  // namespace yasb::platform

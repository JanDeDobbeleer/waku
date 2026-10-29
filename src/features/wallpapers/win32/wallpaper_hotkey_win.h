#pragma once

#include <memory>

#include "features/wallpapers/wallpaper_hotkey.h"

namespace wabar::wallpapers {

// The real hotkey host: RegisterHotKey on the calling (GUI) thread.
std::unique_ptr<HotkeyHost> makeWin32HotkeyHost();

}  // namespace wabar::wallpapers

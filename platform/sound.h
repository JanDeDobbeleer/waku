#pragma once

#include <string>

namespace yasb::platform {

// Plays a .wav file asynchronously (winmm PlaySound). `loop` repeats it until stopSound(). Falls
// back to the "SystemNotification" sound alias when `wavPath` is empty or missing. Never throws.
void playSound(const std::wstring& wavPath, bool loop);
void stopSound();

}  // namespace yasb::platform

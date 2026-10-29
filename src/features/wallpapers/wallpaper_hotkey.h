#pragma once

#include <QString>
#include <functional>
#include <optional>

namespace wabar::wallpapers {

// A global hotkey as RegisterHotKey wants it: MOD_ALT 1, MOD_CONTROL 2, MOD_SHIFT 4, MOD_WIN 8.
struct HotkeyCombo {
  unsigned modifiers = 0;
  unsigned virtualKey = 0;
};
// "alt+e", "ctrl+shift+f5", "win+space": modifiers alt/ctrl/control/shift/win/windows/super/meta
// plus one letter, digit, F1..F24 or space. Case and spaces do not matter. nullopt otherwise.
std::optional<HotkeyCombo> parseHotkey(const QString& keys);

// Where the hotkey lives. The real one calls RegisterHotKey on the GUI thread and receives
// WM_HOTKEY through a native event filter in Qt's own message loop; tests replace it.
class HotkeyHost {
 public:
  virtual ~HotkeyHost() = default;
  // False when the combination is taken or the call fails (the caller logs and carries on).
  virtual bool registerKey(const HotkeyCombo& combo, std::function<void()> pressed) = 0;
  virtual void unregisterKey() = 0;
};

}  // namespace wabar::wallpapers

#pragma once

#include <QString>
#include <optional>

#include "kernel/qt/hotkeys.h"

namespace waku::keymap {

// Key names of a whkd file: the Windows virtual-key names without the `VK_` prefix (the list whkd
// takes from the win-hotkeys crate), in any case, plus a few aliases (`enter`, `esc`, `pageup`).
// A letter or a digit stands for itself. nullopt for an unknown name or a modifier.
std::optional<unsigned> virtualKeyForName(const QString& name);

// alt, ctrl, shift and win (and lalt, rctrl ...: RegisterHotKey cannot tell left from right, so
// both sides give the same modifier) as the MOD_* bit; nullopt for anything else.
std::optional<unsigned> modifierForName(const QString& name);

struct ComboParse {
  std::optional<kernel::HotkeyCombo> combo;
  QString error;  // set when combo is empty
};

// "alt + shift + oem_4": modifiers and exactly one key, separated by `+`.
ComboParse parseCombo(const QString& text);

}  // namespace waku::keymap

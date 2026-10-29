#include "features/wallpapers/wallpaper_hotkey.h"

#include <QStringList>

namespace wabar::wallpapers {

std::optional<HotkeyCombo> parseHotkey(const QString& keys) {
  HotkeyCombo combo;
  for (QString part : keys.split(QLatin1Char('+'))) {
    part = part.simplified().remove(QLatin1Char(' ')).toLower();
    if (part.isEmpty()) continue;
    if (part == QLatin1String("alt")) {
      combo.modifiers |= 1;
    } else if (part == QLatin1String("ctrl") || part == QLatin1String("control")) {
      combo.modifiers |= 2;
    } else if (part == QLatin1String("shift")) {
      combo.modifiers |= 4;
    } else if (part == QLatin1String("win") || part == QLatin1String("windows") ||
               part == QLatin1String("super") || part == QLatin1String("meta")) {
      combo.modifiers |= 8;
    } else {
      if (combo.virtualKey != 0) return std::nullopt;
      if (part.size() == 1 && ((part[0] >= QLatin1Char('a') && part[0] <= QLatin1Char('z')) ||
                               (part[0] >= QLatin1Char('0') && part[0] <= QLatin1Char('9')))) {
        combo.virtualKey = static_cast<unsigned>(part[0].toUpper().unicode());
      } else if (part == QLatin1String("space")) {
        combo.virtualKey = 0x20u /* VK_SPACE */;
      } else if (part.size() >= 2 && part[0] == QLatin1Char('f')) {
        bool ok = false;
        const int number = part.mid(1).toInt(&ok);
        if (!ok || number < 1 || number > 24) return std::nullopt;
        combo.virtualKey = static_cast<unsigned>(0x70 /* VK_F1 */ + number - 1);
      } else {
        return std::nullopt;
      }
    }
  }
  if (combo.virtualKey == 0) return std::nullopt;
  return combo;
}

}  // namespace wabar::wallpapers

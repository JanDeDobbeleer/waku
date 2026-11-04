#include "features/keymap/key_names.h"

#include <QHash>
#include <QStringList>

namespace waku::keymap {
namespace {

const QHash<QString, unsigned>& namedKeys() {
  static const QHash<QString, unsigned> keys = [] {
    QHash<QString, unsigned> table = {
        {QStringLiteral("back"), 0x08},
        {QStringLiteral("backspace"), 0x08},
        {QStringLiteral("tab"), 0x09},
        {QStringLiteral("clear"), 0x0C},
        {QStringLiteral("return"), 0x0D},
        {QStringLiteral("enter"), 0x0D},
        {QStringLiteral("pause"), 0x13},
        {QStringLiteral("capital"), 0x14},
        {QStringLiteral("capslock"), 0x14},
        {QStringLiteral("escape"), 0x1B},
        {QStringLiteral("esc"), 0x1B},
        {QStringLiteral("space"), 0x20},
        {QStringLiteral("prior"), 0x21},
        {QStringLiteral("pageup"), 0x21},
        {QStringLiteral("next"), 0x22},
        {QStringLiteral("pagedown"), 0x22},
        {QStringLiteral("end"), 0x23},
        {QStringLiteral("home"), 0x24},
        {QStringLiteral("left"), 0x25},
        {QStringLiteral("up"), 0x26},
        {QStringLiteral("right"), 0x27},
        {QStringLiteral("down"), 0x28},
        {QStringLiteral("select"), 0x29},
        {QStringLiteral("print"), 0x2A},
        {QStringLiteral("execute"), 0x2B},
        {QStringLiteral("snapshot"), 0x2C},
        {QStringLiteral("printscreen"), 0x2C},
        {QStringLiteral("insert"), 0x2D},
        {QStringLiteral("ins"), 0x2D},
        {QStringLiteral("delete"), 0x2E},
        {QStringLiteral("del"), 0x2E},
        {QStringLiteral("help"), 0x2F},
        {QStringLiteral("apps"), 0x5D},
        {QStringLiteral("sleep"), 0x5F},
        {QStringLiteral("multiply"), 0x6A},
        {QStringLiteral("add"), 0x6B},
        {QStringLiteral("separator"), 0x6C},
        {QStringLiteral("subtract"), 0x6D},
        {QStringLiteral("decimal"), 0x6E},
        {QStringLiteral("divide"), 0x6F},
        {QStringLiteral("numlock"), 0x90},
        {QStringLiteral("scroll"), 0x91},
        {QStringLiteral("browser_back"), 0xA6},
        {QStringLiteral("browser_forward"), 0xA7},
        {QStringLiteral("browser_refresh"), 0xA8},
        {QStringLiteral("browser_stop"), 0xA9},
        {QStringLiteral("browser_search"), 0xAA},
        {QStringLiteral("browser_favorites"), 0xAB},
        {QStringLiteral("browser_home"), 0xAC},
        {QStringLiteral("volume_mute"), 0xAD},
        {QStringLiteral("volume_down"), 0xAE},
        {QStringLiteral("volume_up"), 0xAF},
        {QStringLiteral("media_next_track"), 0xB0},
        {QStringLiteral("media_prev_track"), 0xB1},
        {QStringLiteral("media_stop"), 0xB2},
        {QStringLiteral("media_play_pause"), 0xB3},
        {QStringLiteral("launch_mail"), 0xB4},
        {QStringLiteral("launch_media_select"), 0xB5},
        {QStringLiteral("launch_app1"), 0xB6},
        {QStringLiteral("launch_app2"), 0xB7},
        {QStringLiteral("oem_1"), 0xBA},
        {QStringLiteral("oem_plus"), 0xBB},
        {QStringLiteral("oem_comma"), 0xBC},
        {QStringLiteral("oem_minus"), 0xBD},
        {QStringLiteral("oem_period"), 0xBE},
        {QStringLiteral("oem_2"), 0xBF},
        {QStringLiteral("oem_3"), 0xC0},
        {QStringLiteral("oem_4"), 0xDB},
        {QStringLiteral("oem_5"), 0xDC},
        {QStringLiteral("oem_6"), 0xDD},
        {QStringLiteral("oem_7"), 0xDE},
        {QStringLiteral("oem_8"), 0xDF},
        {QStringLiteral("oem_102"), 0xE2},
        {QStringLiteral("attn"), 0xF6},
        {QStringLiteral("crsel"), 0xF7},
        {QStringLiteral("exsel"), 0xF8},
        {QStringLiteral("play"), 0xFA},
        {QStringLiteral("zoom"), 0xFB},
        {QStringLiteral("pa1"), 0xFD},
        {QStringLiteral("oem_clear"), 0xFE},
    };
    for (int i = 0; i <= 9; ++i) {
      table.insert(QStringLiteral("numpad%1").arg(i), 0x60u + static_cast<unsigned>(i));
    }
    for (int i = 1; i <= 24; ++i) {
      table.insert(QStringLiteral("f%1").arg(i), 0x70u + static_cast<unsigned>(i) - 1u);
    }
    return table;
  }();
  return keys;
}

}  // namespace

std::optional<unsigned> virtualKeyForName(const QString& name) {
  const QString lower = name.trimmed().toLower();
  if (lower.size() == 1) {
    const QChar c = lower.at(0);
    if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
        (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
      return static_cast<unsigned>(c.toUpper().unicode());
    }
    return std::nullopt;
  }
  const auto found = namedKeys().constFind(lower);
  if (found == namedKeys().constEnd()) return std::nullopt;
  return found.value();
}

std::optional<unsigned> modifierForName(const QString& name) {
  const QString lower = name.trimmed().toLower();
  if (lower == QLatin1String("alt") || lower == QLatin1String("lalt") ||
      lower == QLatin1String("ralt") || lower == QLatin1String("menu") ||
      lower == QLatin1String("lmenu") || lower == QLatin1String("rmenu")) {
    return 1u;  // MOD_ALT
  }
  if (lower == QLatin1String("ctrl") || lower == QLatin1String("control") ||
      lower == QLatin1String("lctrl") || lower == QLatin1String("rctrl") ||
      lower == QLatin1String("lcontrol") || lower == QLatin1String("rcontrol")) {
    return 2u;  // MOD_CONTROL
  }
  if (lower == QLatin1String("shift") || lower == QLatin1String("lshift") ||
      lower == QLatin1String("rshift")) {
    return 4u;  // MOD_SHIFT
  }
  if (lower == QLatin1String("win") || lower == QLatin1String("lwin") ||
      lower == QLatin1String("rwin")) {
    return 8u;  // MOD_WIN
  }
  return std::nullopt;
}

ComboParse parseCombo(const QString& text) {
  ComboParse result;
  kernel::HotkeyCombo combo;
  bool haveKey = false;
  const QStringList parts = text.split(QLatin1Char('+'));
  for (const QString& raw : parts) {
    const QString part = raw.trimmed();
    if (part.isEmpty()) {
      result.error = QStringLiteral("empty key name in '%1'").arg(text.trimmed());
      return result;
    }
    if (const std::optional<unsigned> modifier = modifierForName(part)) {
      combo.modifiers |= *modifier;
      continue;
    }
    const std::optional<unsigned> key = virtualKeyForName(part);
    if (!key) {
      result.error = QStringLiteral("unknown key '%1'").arg(part);
      return result;
    }
    if (haveKey) {
      result.error =
          QStringLiteral("two keys in '%1' (only modifiers may be combined)").arg(text.trimmed());
      return result;
    }
    combo.virtualKey = *key;
    haveKey = true;
  }
  if (!haveKey) {
    result.error = QStringLiteral("no key in '%1'").arg(text.trimmed());
    return result;
  }
  result.combo = combo;
  return result;
}

}  // namespace waku::keymap

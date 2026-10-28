#include "widgets/fluent/fluent_theme.h"

#include <QGuiApplication>
#include <QHash>
#include <QStyleHints>
#include <optional>

#include "platform/accent_color.h"

namespace yasb::widgets::fluent {
namespace {

struct Token {
  const char* key;
  const char* value;
};

const Token kDarkTokens[] = {
    {"text_primary", "#ffffff"},
    {"text_secondary", "#b4ffffff"},
    {"text_disabled", "#5cffffff"},
    {"text_on_accent_primary", "#000000"},
    {"text_on_accent_secondary", "#80000000"},
    {"control_fill_default", "#0fffffff"},
    {"control_fill_secondary", "#14ffffff"},
    {"control_fill_tertiary", "#08ffffff"},
    {"control_fill_disabled", "#0affffff"},
    {"control_stroke_default", "#02ffffff"},
    {"control_stroke_secondary", "#08ffffff"},
    {"control_stroke_on_accent_default", "#14ffffff"},
    {"control_stroke_on_accent_secondary", "#23000000"},
    {"accent_fill_default", "#4cc2ff"},
    {"accent_fill_secondary", "#e64cc2ff"},
    {"accent_fill_tertiary", "#cc4cc2ff"},
    {"solid_bg_base", "#202020"},
    {"layer_default", "#4c3a3a3a"},
    {"layer_alt", "#0dffffff"},
};

const Token kLightTokens[] = {
    {"text_primary", "#e3000000"},
    {"text_secondary", "#9e000000"},
    {"text_disabled", "#5c000000"},
    {"text_on_accent_primary", "#ffffff"},
    {"text_on_accent_secondary", "#b2ffffff"},
    {"control_fill_default", "#b2ffffff"},
    {"control_fill_secondary", "#80f9f9f9"},
    {"control_fill_tertiary", "#4cf9f9f9"},
    {"control_fill_disabled", "#4cf9f9f9"},
    {"control_stroke_default", "#29000000"},
    {"control_stroke_secondary", "#0f000000"},
    {"control_stroke_on_accent_default", "#14ffffff"},
    {"control_stroke_on_accent_secondary", "#66000000"},
    {"accent_fill_default", "#0078d4"},
    {"accent_fill_secondary", "#e60078d4"},
    {"accent_fill_tertiary", "#cc0078d4"},
    {"solid_bg_base", "#f3f3f3"},
    {"layer_default", "#80ffffff"},
    {"layer_alt", "#ffffff"},
};

const char* const kAccentKeys[] = {"accent_fill_default", "accent_fill_secondary",
                                   "accent_fill_tertiary"};

bool isAccentKey(const QString& key) {
  for (const char* accent : kAccentKeys) {
    if (key == QLatin1String(accent)) return true;
  }
  return false;
}

// The system accent is read once (Python caches it in _accent_cache).
std::optional<QString> systemAccent(bool dark) {
  static const std::optional<QString> cache[2] = {
      [] {
        const auto rgb = platform::systemAccentShadeRgb(platform::AccentShade::Dark1);
        return rgb ? std::optional<QString>(
                         QStringLiteral("#%1").arg(*rgb, 6, 16, QLatin1Char('0')))
                   : std::nullopt;
      }(),
      [] {
        const auto rgb = platform::systemAccentShadeRgb(platform::AccentShade::Light2);
        return rgb ? std::optional<QString>(
                         QStringLiteral("#%1").arg(*rgb, 6, 16, QLatin1Char('0')))
                   : std::nullopt;
      }()};
  return cache[dark ? 1 : 0];
}

}  // namespace

bool isDark() {
  const QStyleHints* hints = qGuiApp != nullptr ? qGuiApp->styleHints() : nullptr;
  if (hints == nullptr) return true;
  return hints->colorScheme() == Qt::ColorScheme::Dark;
}

QString recolour(const QString& value, const QString& rgb) {
  return value.size() == 9 ? value.left(3) + rgb.mid(1) : rgb;
}

QString token(const QString& key, bool dark) {
  QString value;
  for (const Token& entry : dark ? kDarkTokens : kLightTokens) {
    if (key == QLatin1String(entry.key)) {
      value = QString::fromLatin1(entry.value);
      break;
    }
  }
  if (value.isEmpty()) return value;
  if (isAccentKey(key)) {
    if (const auto accent = systemAccent(dark)) return recolour(value, *accent);
  }
  return value;
}

QColor tokenColor(const QString& key, bool dark) {
  const QString value = token(key, dark);
  return value.isEmpty() ? QColor(Qt::transparent) : QColor(value);
}

QStringList fontFamilies() {
  return {QStringLiteral("Segoe UI Variable"), QStringLiteral("Segoe UI"),
          QStringLiteral("system-ui")};
}

}  // namespace yasb::widgets::fluent

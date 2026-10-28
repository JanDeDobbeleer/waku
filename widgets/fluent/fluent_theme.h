#pragma once

#include <QColor>
#include <QString>

namespace yasb::widgets::fluent {

// Port of the subset of Python's core/ui/tokens.py + theme.py that the GitHub auth dialog needs.
// Values are "#AARRGGBB" like the Python tokens. The accent tokens follow the Windows accent
// colour (AccentLight2 on dark, AccentDark1 on light) when WinRT supplies it.
bool isDark();
QString token(const QString& key, bool dark);
inline QString token(const QString& key) { return token(key, isDark()); }
QColor tokenColor(const QString& key, bool dark);
inline QColor tokenColor(const QString& key) { return tokenColor(key, isDark()); }

// Swaps the RGB of a token, keeping an existing alpha prefix (theme.py `_recolour`). `rgb` is
// "#rrggbb".
QString recolour(const QString& value, const QString& rgb);

// Python FONT_FAMILIES.
QStringList fontFamilies();

}  // namespace yasb::widgets::fluent

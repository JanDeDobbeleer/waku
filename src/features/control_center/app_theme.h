#pragma once

#include <QString>
#include <optional>

namespace waku::control_center {

// Windows app theme (services/control_center/api/theme.py ThemeService). Modes: "light", "dark".

// "dark" when the registry value cannot be read.
QString themeMode();
// Writes AppsUseLightTheme and SystemUsesLightTheme and broadcasts WM_SETTINGCHANGE.
bool setThemeMode(const QString& mode);
// The new mode, or nullopt when the write failed.
std::optional<QString> toggleThemeMode();

}  // namespace waku::control_center

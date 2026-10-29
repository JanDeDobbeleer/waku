#include <windows.h>

#include <QDebug>

#include "features/control_center/app_theme.h"

namespace wabar::control_center {
namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

}  // namespace

QString themeMode() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                   nullptr, &value, &size) != ERROR_SUCCESS) {
    qWarning() << "Failed to read Windows theme mode";
    return QStringLiteral("dark");
  }
  return value ? QStringLiteral("light") : QStringLiteral("dark");
}

bool setThemeMode(const QString& mode) {
  const DWORD value = mode == QLatin1String("light") ? 1 : 0;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    qWarning().noquote() << "Failed to set Windows theme mode to" << mode;
    return false;
  }
  const bool ok =
      RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                     sizeof(value)) == ERROR_SUCCESS &&
      RegSetValueExW(key, L"SystemUsesLightTheme", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
  RegCloseKey(key);
  if (!ok) {
    qWarning().noquote() << "Failed to set Windows theme mode to" << mode;
    return false;
  }
  static wchar_t area[] = L"ImmersiveColorSet";
  if (!SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(area)))
    qWarning().noquote() << "Theme set to" << mode << "but broadcast notification failed.";
  return true;
}

std::optional<QString> toggleThemeMode() {
  const QString next =
      themeMode() == QLatin1String("dark") ? QStringLiteral("light") : QStringLiteral("dark");
  if (setThemeMode(next)) return next;
  return std::nullopt;
}

}  // namespace wabar::control_center

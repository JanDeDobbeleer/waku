#include "core/bar/theme_class.h"

#include <windows.h>

#include <QPointer>
#include <QStyle>

namespace yasb::bar {

void GlobalState::setDark(bool dark) {
  if (dark_ == dark) return;
  dark_ = dark;
  emit darkChanged(dark);
}

bool detectOsDarkTheme() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LSTATUS status = RegGetValueW(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
  return status == ERROR_SUCCESS && value == 0;
}

void refreshWidgetStyle(QWidget* widget) {
  if (!widget) return;
  if (QStyle* style = widget->style()) {
    style->unpolish(widget);
    style->polish(widget);
  }
}

OsThemeManager::OsThemeManager(QWidget* target, QObject* parent, Detector detector,
                               GlobalState* globalState)
    : QObject(parent), target_(target), detector_(std::move(detector)), globalState_(globalState) {}

void OsThemeManager::updateThemeClass() {
  if (!target_) return;
  const bool dark = detector_();
  if (isDark_ && *isDark_ == dark) return;

  QString cls = target_->property("class").toString();
  if (dark) {
    cls += QStringLiteral(" dark");
  } else {
    cls.replace(QStringLiteral(" dark"), QString());
  }
  target_->setProperty("class", cls);
  refreshWidgetStyle(target_);
  for (QWidget* child : target_->findChildren<QWidget*>()) refreshWidgetStyle(child);
  isDark_ = dark;
  if (globalState_) globalState_->setDark(dark);
}

}  // namespace yasb::bar

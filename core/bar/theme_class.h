#pragma once

#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <optional>

namespace yasb::bar {

// Process-wide UI state shared by bars and widgets (port of bar_helper.GlobalState, dark flag
// only).
class GlobalState : public QObject {
  Q_OBJECT

 public:
  static GlobalState& instance();
  bool isDark() const { return dark_; }
  void setDark(bool dark);

 signals:
  void darkChanged(bool dark);

 private:
  GlobalState() = default;
  bool dark_ = false;
};

// Reads HKCU\...\Personalize\AppsUseLightTheme; dark when 0, light when unreadable.
bool detectOsDarkTheme();

// Keeps a " dark" suffix on the target's `class` property in line with the OS theme and
// repolishes the target and its descendants when it flips (port of bar_helper.OsThemeManager).
class OsThemeManager : public QObject {
  Q_OBJECT

 public:
  using Detector = std::function<bool()>;
  explicit OsThemeManager(QWidget* target, QObject* parent = nullptr,
                          Detector detector = detectOsDarkTheme);

  void updateThemeClass();

 private:
  QPointer<QWidget> target_;
  Detector detector_;
  std::optional<bool> isDark_;
};

// style()->unpolish + polish (utilities.refresh_widget_style).
void refreshWidgetStyle(QWidget* widget);

}  // namespace yasb::bar

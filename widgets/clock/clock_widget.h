#pragma once

#include <QDateTime>
#include <QLocale>
#include <QPointer>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/stat_popup.h"
#include "widgets/clock/clock_state.h"
#include "widgets/clock/py_strftime.h"

class QButtonGroup;
class QCalendarWidget;
class QFrame;
class QSpinBox;

namespace yasb::widgets {

// A moment of wall-clock time: the UTC instant plus the microseconds Python's %f prints.
struct ClockInstant {
  QDateTime utc;
  int microsecond = 0;
};
ClockInstant systemClockInstant();

// Port of core/widgets/yasb/clock.py (ClockWidget): label with strftime placeholders, alarm icon,
// timer label, calendar popup, context menu and the alarm and timer dialogs. Alarms, snoozes and
// the timer live in ClockSharedState; the first clock widget drives its once-per-interval tick.
//
// Thread: GUI thread only; no worker threads.
class ClockWidget final : public BaseWidget, public ClockClient {
  Q_OBJECT
 public:
  using InstantFn = std::function<ClockInstant()>;

  explicit ClockWidget(const WidgetContext& context, ClockSharedState* state = nullptr,
                       InstantFn clock = {});
  ~ClockWidget() override;

  // ClockClient
  void refreshClock(bool updateTooltip) override;
  void startTicking() override;
  void alarmFired(const Alarm& alarm) override;
  void timerFinished() override;

  void render();
  void updateTooltip();
  QString tooltipText() const;
  QString timerText() const;

  void nextTimezone();
  void setTimezone(const std::optional<std::string>& timezone);
  const std::optional<std::string>& activeTimezone() const { return activeTz_; }
  const std::vector<std::optional<std::string>>& timezones() const { return timezoneList_; }

  // Popups. Each replaces the previous one of its kind.
  void showCalendar();
  void showContextMenu();
  void showAlarmDialog(const Alarm* alarm = nullptr);
  void showTimerDialog();
  void showActiveAlarm(const Alarm& alarm);
  QWidget* calendarPopup() const { return calendarPopup_; }
  QWidget* alarmPopup() const { return alarmPopup_; }
  QWidget* timerPopup() const { return timerPopup_; }
  QWidget* activeAlarmPopup() const { return activeAlarm_; }

 private:
  struct Moment {
    QDateTime when;  // in the active zone
    int microsecond = 0;
    QString tzName;
  };
  Moment now() const;
  QString iconForHour(int hour) const;
  QString localeName() const { return QString::fromStdString(options_.locale); }
  QLocale qlocale() const;
  PinnablePopup* newPopup(const QString& cssClass);
  void positionPopup(PinnablePopup* popup);
  void playSound(bool loop);
  void stopAlarmSound();
  QHBoxLayout* timeGrid(const QString& label1, const QString& label2, int max1, int max2,
                        int value1, int value2, QSpinBox** spin1, QSpinBox** spin2);
  QFrame* footer(const std::vector<QPushButton*>& buttons);
  static void clearQuickGroup(QButtonGroup* group);

  config::options::ClockConfig options_;
  bar::GlobalState& globalState_;
  ClockSharedState& state_;
  InstantFn clock_;
  PyStrftime formatter_;
  QString label_;
  QString labelAlt_;
  QLabel* timerLabel_ = nullptr;
  bool timerVisible_ = false;
  std::optional<std::string> activeTz_;
  std::vector<std::optional<std::string>> timezoneList_;
  std::vector<std::optional<std::string>> timezoneCycle_;
  size_t cyclePos_ = 0;
  QPointer<PinnablePopup> calendarPopup_;
  QPointer<PinnablePopup> alarmPopup_;
  QPointer<PinnablePopup> timerPopup_;
  QPointer<QFrame> activeAlarm_;
  QPointer<QLabel> dayLabel_;
  QPointer<QLabel> monthLabel_;
  QPointer<QLabel> yearLabel_;
  QPointer<QLabel> dateLabel_;
  QPointer<QLabel> weekLabel_;
  QPointer<QCalendarWidget> calendar_;
};

}  // namespace yasb::widgets

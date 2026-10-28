#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace yasb::widgets {

// An alarm is the JSON object Python keeps in alarms.json ({"time","days","enabled","title"}).
// Keys the file has beyond those survive a load and save, and key order is preserved.
using Alarm = nlohmann::ordered_json;

std::string alarmTime(const Alarm& alarm);
std::vector<int> alarmDays(const Alarm& alarm);  // empty or 7 entries mean every day
bool alarmEnabled(const Alarm& alarm);           // missing means enabled
std::string alarmTitle(const Alarm& alarm);

// Label of an alarm in the right-click menu: "<icon>  07:00 (Mon, Tue)".
QString formatAlarmText(const Alarm& alarm, const QString& enabledIcon,
                        const QString& disabledIcon);

struct SnoozedAlarm {
  Alarm alarm;
  QDateTime until;
};

// Multi-line tooltip block (ClockWidget._get_alarms_tooltip).
QString alarmsTooltip(const std::vector<Alarm>& alarms, const std::vector<SnoozedAlarm>& snoozed);

// Python's json.dumps(alarms, indent=2, ensure_ascii=False) with integer lists collapsed to one
// line ("[0,1,2]"), and CRLF line endings as a text-mode write produces on Windows.
std::string serializeAlarms(const std::vector<Alarm>& alarms);

// A clock widget as the shared state sees it. The first registered one drives the tick and shows
// the alarm UI.
class ClockClient {
 public:
  virtual ~ClockClient() = default;
  virtual void refreshClock(bool updateTooltip) = 0;
  virtual void startTicking() = 0;
  virtual void alarmFired(const Alarm& alarm) = 0;
  virtual void timerFinished() = 0;
};

// State every clock widget shares (ClockWidgetSharedState): alarms, snoozed alarms and the
// countdown timer. alarms.json is the file Python YASB keeps in the config folder.
//
// Thread: GUI thread only. Owned by qApp; clients unregister when they are destroyed.
class ClockSharedState : public QObject {
  Q_OBJECT
 public:
  using NowFn = std::function<QDateTime()>;

  explicit ClockSharedState(std::filesystem::path alarmsFile, NowFn now = {},
                            QObject* parent = nullptr);

  // Application-wide instance, created on first use in the config folder.
  static ClockSharedState& instance();
  // Test seam: replaces the instance (the previous one is deleted).
  static ClockSharedState& resetInstance(std::filesystem::path alarmsFile, NowFn now = {});

  void registerClient(ClockClient* client);
  void unregisterClient(ClockClient* client);
  const std::vector<ClockClient*>& clients() const { return clients_; }

  // One timer tick: countdown, snooze expiry, alarm check on a new minute, then a refresh.
  void tick();

  const std::vector<Alarm>& alarms() const { return alarms_; }
  const std::vector<SnoozedAlarm>& snoozed() const { return snoozed_; }
  bool hasEnabledAlarms() const;
  bool timerActive() const { return timerActive_; }
  int timerSecondsRemaining() const { return timerSeconds_; }

  void addAlarm(Alarm alarm);
  // Edits in place; a snoozed copy follows the edit and is dropped when the new alarm is disabled.
  bool updateAlarm(const Alarm& previous, Alarm updated);
  bool removeAlarm(const Alarm& alarm);
  // Replaces the in-memory list; nothing is written to the file.
  void replaceAlarms(std::vector<Alarm> alarms) {
    alarms_ = std::move(alarms);
    notifyAll(true);
  }
  void clearSnoozed() {
    snoozed_.clear();
    notifyAll(true);
  }
  void snooze(const Alarm& alarm, int minutes);
  void clearSnooze(const Alarm& alarm);

  void startTimer(int seconds);
  void cancelTimer();

  void notifyAll(bool updateTooltip = false);

  bool load();
  bool save() const;
  const std::filesystem::path& alarmsFile() const { return file_; }

 private:
  void checkAlarms();
  QDateTime now() const;

  std::filesystem::path file_;
  NowFn now_;
  std::vector<ClockClient*> clients_;
  std::vector<Alarm> alarms_;
  std::vector<SnoozedAlarm> snoozed_;
  std::set<int> triggered_;
  QString lastCheckMinute_;
  QString startupMinute_;
  int timerSeconds_ = 0;
  bool timerActive_ = false;
};

}  // namespace yasb::widgets

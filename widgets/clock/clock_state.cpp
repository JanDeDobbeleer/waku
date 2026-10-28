#include "widgets/clock/clock_state.h"

#include <QApplication>
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/config/paths.h"

namespace yasb::widgets {
namespace {

const char* const kShortDays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char* const kLongDays[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};

QString dayName(const char* const* names, int index) {
  if (index < 0 || index > 6) return QString::number(index);
  return QString::fromLatin1(names[index]);
}

QString daysText(const std::vector<int>& daysIn, bool longSingle) {
  if (daysIn.empty() || daysIn.size() == 7) return QStringLiteral("Every day");
  if (daysIn.size() == 1) return dayName(longSingle ? kLongDays : kShortDays, daysIn.front());
  std::vector<int> days = daysIn;
  std::sort(days.begin(), days.end());
  QStringList names;
  for (int day : days) names << dayName(kShortDays, day);
  return names.join(QStringLiteral(", "));
}

QString clockMinute(const QDateTime& when) { return when.toString(QStringLiteral("HH:mm")); }

QPointer<ClockSharedState>& sharedInstance() {
  static QPointer<ClockSharedState> instance;
  return instance;
}

}  // namespace

std::string alarmTime(const Alarm& alarm) {
  const auto it = alarm.find("time");
  return it != alarm.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::vector<int> alarmDays(const Alarm& alarm) {
  std::vector<int> days;
  const auto it = alarm.find("days");
  if (it == alarm.end() || !it->is_array()) return days;
  for (const auto& day : *it) {
    if (day.is_number_integer()) days.push_back(day.get<int>());
  }
  return days;
}

bool alarmEnabled(const Alarm& alarm) {
  const auto it = alarm.find("enabled");
  return it == alarm.end() || !it->is_boolean() || it->get<bool>();
}

std::string alarmTitle(const Alarm& alarm) {
  const auto it = alarm.find("title");
  return it != alarm.end() && it->is_string() ? it->get<std::string>() : std::string();
}

QString formatAlarmText(const Alarm& alarm, const QString& enabledIcon,
                        const QString& disabledIcon) {
  const QString time = QString::fromStdString(alarmTime(alarm));
  const QString icon = (alarmEnabled(alarm) ? enabledIcon : disabledIcon) + QLatin1Char(' ');
  return QStringLiteral("%1 %2 (%3)").arg(icon, time, daysText(alarmDays(alarm), false));
}

QString alarmsTooltip(const std::vector<Alarm>& alarms, const std::vector<SnoozedAlarm>& snoozed) {
  std::vector<const Alarm*> enabled;
  for (const auto& alarm : alarms) {
    if (alarmEnabled(alarm)) enabled.push_back(&alarm);
  }
  const auto snoozeText = [](const SnoozedAlarm& s) {
    return QStringLiteral("Snoozed alarm returns at %1").arg(s.until.toString("HH:mm:ss"));
  };
  if (!snoozed.empty() && enabled.empty()) {
    if (snoozed.size() == 1) return snoozeText(snoozed.front());
    return QStringLiteral("%1 snoozed alarms").arg(snoozed.size());
  }
  if (enabled.empty()) return QStringLiteral("No active alarms");

  QStringList lines{QStringLiteral("Active Alarms")};
  for (const Alarm* alarm : enabled) {
    lines << QStringLiteral("%1 - %2").arg(QString::fromStdString(alarmTime(*alarm)),
                                           daysText(alarmDays(*alarm), true));
  }
  for (const auto& s : snoozed) lines << snoozeText(s);
  return lines.join(QLatin1Char('\n'));
}

std::string serializeAlarms(const std::vector<Alarm>& alarms) {
  nlohmann::ordered_json array = nlohmann::ordered_json::array();
  for (const auto& alarm : alarms) array.push_back(alarm);
  const QString text = QString::fromStdString(array.dump(2));
  static const QRegularExpression listRe(QStringLiteral("\\[\\s*(\\d+(?:\\s*,\\s*\\d+)*)\\s*\\]"));
  QString out;
  qsizetype last = 0;
  auto it = listRe.globalMatch(text);
  while (it.hasNext()) {
    const auto m = it.next();
    out += text.mid(last, m.capturedStart() - last);
    QString inner = m.captured(1);
    inner.remove(QLatin1Char('\n'));
    inner.remove(QLatin1Char(' '));
    out += QLatin1Char('[') + inner + QLatin1Char(']');
    last = m.capturedEnd();
  }
  out += text.mid(last);
  out.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
  return out.toStdString();
}

ClockSharedState::ClockSharedState(std::filesystem::path alarmsFile, NowFn now, QObject* parent)
    : QObject(parent), file_(std::move(alarmsFile)), now_(std::move(now)) {
  startupMinute_ = clockMinute(this->now());
  load();
}

QDateTime ClockSharedState::now() const { return now_ ? now_() : QDateTime::currentDateTime(); }

ClockSharedState& ClockSharedState::instance() {
  auto& instance = sharedInstance();
  if (!instance) {
    instance = new ClockSharedState(yasb::config::configDir() / "alarms.json", {}, qApp);
  }
  return *instance;
}

ClockSharedState& ClockSharedState::resetInstance(std::filesystem::path alarmsFile, NowFn now) {
  auto& instance = sharedInstance();
  delete instance.data();
  instance = new ClockSharedState(std::move(alarmsFile), std::move(now), qApp);
  return *instance;
}

void ClockSharedState::registerClient(ClockClient* client) {
  if (std::find(clients_.begin(), clients_.end(), client) != clients_.end()) return;
  clients_.push_back(client);
  if (clients_.size() == 1) client->startTicking();
}

void ClockSharedState::unregisterClient(ClockClient* client) {
  clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
}

bool ClockSharedState::hasEnabledAlarms() const {
  return std::any_of(alarms_.begin(), alarms_.end(),
                     [](const Alarm& alarm) { return alarmEnabled(alarm); }) ||
         !snoozed_.empty();
}

void ClockSharedState::tick() {
  if (timerActive_) {
    if (timerSeconds_ == 0) {
      timerActive_ = false;
      if (!clients_.empty()) clients_.front()->timerFinished();
    } else if (timerSeconds_ > 0) {
      --timerSeconds_;
    }
  }

  const QDateTime current = now();
  for (const SnoozedAlarm& snoozed : std::vector<SnoozedAlarm>(snoozed_)) {
    if (current < snoozed.until) continue;
    const auto it = std::find_if(snoozed_.begin(), snoozed_.end(), [&](const SnoozedAlarm& s) {
      return s.alarm == snoozed.alarm && s.until == snoozed.until;
    });
    if (it != snoozed_.end()) snoozed_.erase(it);
    if (!clients_.empty()) clients_.front()->alarmFired(snoozed.alarm);
  }

  const QString minute = clockMinute(now());
  bool minuteChanged = false;
  if (lastCheckMinute_ != minute) {
    lastCheckMinute_ = minute;
    triggered_.clear();
    checkAlarms();
    minuteChanged = true;
  }
  notifyAll(minuteChanged);
}

void ClockSharedState::checkAlarms() {
  const QDateTime current = now();
  const QString minute = clockMinute(current);
  const int day = current.date().dayOfWeek() - 1;
  // Index loop over copies: a client may edit the list while it handles the alarm.
  for (size_t index = 0; index < alarms_.size(); ++index) {
    const Alarm alarm = alarms_[index];
    if (!alarmEnabled(alarm)) continue;
    if (triggered_.count(static_cast<int>(index))) continue;
    if (minute == startupMinute_) continue;
    if (QString::fromStdString(alarmTime(alarm)) != minute) continue;
    const auto days = alarmDays(alarm);
    if (days.empty() || days.size() == 7 ||
        std::find(days.begin(), days.end(), day) != days.end()) {
      triggered_.insert(static_cast<int>(index));
      if (!clients_.empty()) clients_.front()->alarmFired(alarm);
    }
  }
}

void ClockSharedState::addAlarm(Alarm alarm) {
  alarms_.push_back(std::move(alarm));
  save();
  notifyAll(true);
}

bool ClockSharedState::updateAlarm(const Alarm& previous, Alarm updated) {
  const auto it = std::find(alarms_.begin(), alarms_.end(), previous);
  if (it == alarms_.end()) return false;
  // Python edits the dict in place: keys the dialog does not own keep their place and value,
  // and a snoozed entry sees the new values.
  Alarm merged = *it;
  for (auto field = updated.begin(); field != updated.end(); ++field) merged[field.key()] = *field;
  const bool enabled = alarmEnabled(merged);
  for (auto s = snoozed_.begin(); s != snoozed_.end();) {
    if (s->alarm == previous) {
      if (!enabled) {
        s = snoozed_.erase(s);
        continue;
      }
      s->alarm = merged;
    }
    ++s;
  }
  *it = std::move(merged);
  save();
  notifyAll(true);
  return true;
}

bool ClockSharedState::removeAlarm(const Alarm& alarm) {
  const auto it = std::find(alarms_.begin(), alarms_.end(), alarm);
  if (it == alarms_.end()) return false;
  alarms_.erase(it);
  snoozed_.erase(std::remove_if(snoozed_.begin(), snoozed_.end(),
                                [&](const SnoozedAlarm& s) { return s.alarm == alarm; }),
                 snoozed_.end());
  save();
  notifyAll(true);
  return true;
}

void ClockSharedState::snooze(const Alarm& alarm, int minutes) {
  snoozed_.push_back({alarm, now().addSecs(minutes * 60)});
  notifyAll(true);
}

void ClockSharedState::clearSnooze(const Alarm& alarm) {
  snoozed_.erase(std::remove_if(snoozed_.begin(), snoozed_.end(),
                                [&](const SnoozedAlarm& s) { return s.alarm == alarm; }),
                 snoozed_.end());
  notifyAll(true);
}

void ClockSharedState::startTimer(int seconds) {
  timerSeconds_ = seconds;
  timerActive_ = true;
  notifyAll();
}

void ClockSharedState::cancelTimer() {
  timerActive_ = false;
  timerSeconds_ = 0;
  notifyAll();
}

void ClockSharedState::notifyAll(bool updateTooltip) {
  for (ClockClient* client : std::vector<ClockClient*>(clients_)) {
    try {
      client->refreshClock(updateTooltip);
    } catch (const std::exception& error) {
      qWarning().noquote() << "Clock refresh failed:" << error.what();
    }
  }
}

bool ClockSharedState::load() {
  alarms_.clear();
  std::error_code ec;
  if (!std::filesystem::exists(file_, ec)) return true;
  try {
    std::ifstream in(file_, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto parsed = nlohmann::ordered_json::parse(buffer.str(), nullptr, false);
    if (!parsed.is_array()) {
      qWarning().noquote() << "Error loading alarms: not a JSON list";
      return false;
    }
    for (const auto& item : parsed) {
      if (item.is_object()) alarms_.push_back(item);
    }
    return true;
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error loading alarms:" << error.what();
    alarms_.clear();
    return false;
  }
}

bool ClockSharedState::save() const {
  try {
    const std::string text = serializeAlarms(alarms_);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error saving alarms:" << error.what();
    return false;
  }
}

}  // namespace yasb::widgets

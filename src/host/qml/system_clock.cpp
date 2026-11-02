#include "host/qml/system_clock.h"

#include <QTime>

namespace waku::host {

namespace {

std::function<QDateTime()>& timeSource() {
  static std::function<QDateTime()> source;
  return source;
}

QDateTime currentTime() {
  const auto& source = timeSource();
  return source ? source() : QDateTime::currentDateTime();
}

}  // namespace

void SystemClock::setTimeSource(std::function<QDateTime()> source) {
  timeSource() = std::move(source);
}

QDateTime SystemClock::truncated(const QDateTime& now, Precision precision) {
  const QTime time = now.time();
  QTime cut;
  switch (precision) {
    case Hours:
      cut = QTime(time.hour(), 0, 0);
      break;
    case Minutes:
      cut = QTime(time.hour(), time.minute(), 0);
      break;
    case Seconds:
      cut = QTime(time.hour(), time.minute(), time.second());
      break;
  }
  QDateTime result = now;
  result.setTime(cut);
  return result;
}

int SystemClock::msToNextBoundary(const QDateTime& now, Precision precision) {
  const QDateTime start = truncated(now, precision);
  const qint64 unit = precision == Hours ? 3600000 : precision == Minutes ? 60000 : 1000;
  const qint64 left = unit - start.msecsTo(now);
  return static_cast<int>(qMax<qint64>(1, left));
}

SystemClock::SystemClock(QObject* parent) : QObject(parent) {
  timer_.setSingleShot(true);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &SystemClock::tick);
  date_ = truncated(currentTime(), precision_);
}

void SystemClock::componentComplete() {
  complete_ = true;
  tick();
}

void SystemClock::setEnabled(bool enabled) {
  if (enabled == enabled_) return;
  enabled_ = enabled;
  emit enabledChanged();
  if (complete_) tick();
}

void SystemClock::setPrecision(Precision precision) {
  if (precision == precision_) return;
  precision_ = precision;
  emit precisionChanged();
  if (complete_) tick();
}

void SystemClock::tick() {
  timer_.stop();
  if (!enabled_) return;
  const QDateTime now = currentTime();
  refresh();
  timer_.start(msToNextBoundary(now, precision_));
}

void SystemClock::refresh() {
  const QDateTime next = truncated(currentTime(), precision_);
  if (next == date_) return;
  const QDateTime old = date_;
  date_ = next;
  emit dateChanged();
  if (old.time().hour() != next.time().hour()) emit hoursChanged();
  if (old.time().minute() != next.time().minute()) emit minutesChanged();
  if (old.time().second() != next.time().second()) emit secondsChanged();
}

}  // namespace waku::host

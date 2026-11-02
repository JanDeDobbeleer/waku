#pragma once

#include <QtQml/qqmlregistration.h>

#include <QDateTime>
#include <QObject>
#include <QQmlParserStatus>
#include <QTimer>
#include <functional>

namespace waku::host {

// Quickshell's `SystemClock`: the time, updated on the boundary of its precision.
//
//     SystemClock { id: clock; precision: SystemClock.Minutes }
//     Text { text: Qt.formatDateTime(clock.date, "hh:mm") }
//
//   enabled    false pauses the clock (default true)
//   precision  SystemClock.Hours, .Minutes or .Seconds (default Seconds). A change of `date`
//              happens when the precision's unit changes, never between
//   date       the current local date and time, cut to the precision (the finer fields are 0)
//   hours, minutes, seconds  its parts. `minutes` is 0 for Hours precision; `seconds` is 0 for
//              Hours and Minutes
//
// One single-shot timer aimed at the next boundary. Threading: GUI thread only.
class SystemClock : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
  Q_PROPERTY(Precision precision READ precision WRITE setPrecision NOTIFY precisionChanged FINAL)
  Q_PROPERTY(QDateTime date READ date NOTIFY dateChanged FINAL)
  Q_PROPERTY(int hours READ hours NOTIFY hoursChanged FINAL)
  Q_PROPERTY(int minutes READ minutes NOTIFY minutesChanged FINAL)
  Q_PROPERTY(int seconds READ seconds NOTIFY secondsChanged FINAL)

 public:
  enum Precision { Hours = 1, Minutes = 2, Seconds = 3 };
  Q_ENUM(Precision)

  explicit SystemClock(QObject* parent = nullptr);

  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled);
  Precision precision() const { return precision_; }
  void setPrecision(Precision precision);
  QDateTime date() const { return date_; }
  int hours() const { return date_.time().hour(); }
  int minutes() const { return date_.time().minute(); }
  int seconds() const { return date_.time().second(); }

  void classBegin() override {}
  void componentComplete() override;

  // Reads the time again and re-aims the timer. What the timer calls; tests call it after they
  // move their clock.
  void tick();

  // `now` cut to `precision`.
  static QDateTime truncated(const QDateTime& now, Precision precision);
  // Milliseconds from `now` to the next boundary of `precision`, at least 1.
  static int msToNextBoundary(const QDateTime& now, Precision precision);

  // Tests give a fake clock; an empty function restores QDateTime::currentDateTime.
  static void setTimeSource(std::function<QDateTime()> source);

 signals:
  void enabledChanged();
  void precisionChanged();
  void dateChanged();
  void hoursChanged();
  void minutesChanged();
  void secondsChanged();

 private:
  void refresh();

  bool enabled_ = true;
  Precision precision_ = Seconds;
  bool complete_ = false;
  QDateTime date_;
  QTimer timer_;
};

}  // namespace waku::host

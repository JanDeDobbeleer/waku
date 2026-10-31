#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <deque>
#include <memory>
#include <optional>

namespace wabar::kernel {

// Base of the polled data sources (Memory, Cpu, Battery, Network). After begin() it samples every
// `interval` ms while `active`. The platform call runs on a QThreadPool thread, one
// sample at a time per source; the result is applied on the GUI thread and `updated` is emitted.
// A source destroyed while a sample runs drops the result. Stopped by `active: false` or by
// destroying the object (the pool task holds its own reference to the data source).
class PolledSource : public QObject {
  Q_OBJECT
  Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged FINAL)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
  // The last `historySize` samples of the main value (percent), oldest first, for a graph.
  Q_PROPERTY(int historySize READ historySize WRITE setHistorySize NOTIFY historySizeChanged FINAL)
  Q_PROPERTY(QVariantList history READ history NOTIFY updated)

 public:
  explicit PolledSource(QObject* parent = nullptr);

  int interval() const { return interval_; }
  void setInterval(int ms);
  bool active() const { return active_; }
  void setActive(bool active);
  bool ready() const { return ready_; }
  int historySize() const { return historySize_; }
  void setHistorySize(int size);
  QVariantList history() const;
  // Whether the interval timer runs, and whether a sample is on the pool now. With both false no
  // sample can start until begin(), setActive(true) or setInterval(); tests check that instead
  // of waiting.
  bool polling() const { return timer_.isActive(); }
  bool sampling() const { return inFlight_; }

  // Starts polling (once the owner is ready: a QML component is complete, or a singleton is built).
  // Samples at once when `active`.
  void begin();

 signals:
  void intervalChanged();
  void activeChanged();
  void readyChanged();
  void updated();
  void historySizeChanged();

 protected:
  // Appends one sample to the history ring, dropping the oldest beyond `historySize`.
  void recordHistory(double value);

  // Starts one asynchronous sample; implementations call sampleAsync().
  virtual void sample() = 0;

  // Runs source->sample() on the pool, then apply(result) on the GUI thread.
  template <class Source, class Apply>
  void sampleAsync(std::shared_ptr<Source> source, Apply apply) {
    if (inFlight_) return;
    inFlight_ = true;
    QPointer<PolledSource> guard(this);
    QThreadPool::globalInstance()->start(
        [source = std::move(source), apply = std::move(apply), guard]() mutable {
          using Result = decltype(source->sample());
          std::optional<Result> result;
          try {
            result = source->sample();
          } catch (...) {
          }
          QMetaObject::invokeMethod(
              qApp, [guard, apply = std::move(apply), result = std::move(result)]() mutable {
                if (!guard) return;
                guard->inFlight_ = false;
                if (!result) return;
                apply(std::move(*result));
                guard->setReady();
                emit guard->updated();
              });
        });
  }

 private:
  void restart();
  void setReady();

  QTimer timer_;
  int interval_ = 2000;
  bool active_ = true;
  bool ready_ = false;
  bool begun_ = false;
  bool inFlight_ = false;
  int historySize_ = 60;
  std::deque<double> history_;
};

}  // namespace wabar::kernel

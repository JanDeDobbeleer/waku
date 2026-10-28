#pragma once

#include <QObject>
#include <QTimer>
#include <memory>

#include "platform/pin_support.h"

namespace yasb::widgets {

// RecycleBinMonitor for one taskbar widget: while a pinned or open Recycle Bin button exists it
// asks the shell for the bin's item count and size every `intervalMs` and emits `binUpdated` when
// either changed. Python watches the bin folders for changes and polls in bursts; polling a cheap
// SHQueryRecycleBin on a worker keeps this simple. GUI thread owns the monitor; the query runs on
// the global thread pool and returns with a queued call. Stopped by stop() or the destructor (a
// query in flight is dropped).
class RecycleBinMonitor final : public QObject {
  Q_OBJECT

 public:
  explicit RecycleBinMonitor(std::shared_ptr<platform::PinSupport> pins, int intervalMs = 2000,
                             bool async = true, QObject* parent = nullptr);

  void start();
  void stop();
  bool active() const { return timer_.isActive(); }
  // The last answer (zero items before the first one).
  platform::RecycleBinInfo last() const { return last_; }
  // Asks now; a query already running is not repeated.
  void refresh();

 signals:
  void binUpdated(qint64 numItems, qint64 sizeBytes);

 private:
  void apply(const platform::RecycleBinInfo& info);

  std::shared_ptr<platform::PinSupport> pins_;
  bool async_;
  QTimer timer_;
  platform::RecycleBinInfo last_;
  bool pending_ = false;
  bool generation_ = false;
};

}  // namespace yasb::widgets

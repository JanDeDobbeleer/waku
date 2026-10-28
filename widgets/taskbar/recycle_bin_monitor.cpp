#include "widgets/taskbar/recycle_bin_monitor.h"

#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

namespace yasb::widgets {

RecycleBinMonitor::RecycleBinMonitor(std::shared_ptr<platform::PinSupport> pins, int intervalMs,
                                     bool async, QObject* parent)
    : QObject(parent), pins_(std::move(pins)), async_(async) {
  timer_.setInterval(intervalMs);
  connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
}

void RecycleBinMonitor::start() {
  if (timer_.isActive()) return;
  timer_.start();
  refresh();
}

void RecycleBinMonitor::stop() {
  timer_.stop();
  // An answer still on its way belongs to a monitor that is no longer wanted.
  generation_ = !generation_;
  pending_ = false;
}

void RecycleBinMonitor::refresh() {
  if (pending_) return;
  if (!async_) {
    apply(pins_->recycleBinInfo());
    return;
  }
  pending_ = true;
  QPointer<RecycleBinMonitor> self(this);
  const bool generation = generation_;
  auto pins = pins_;
  QThreadPool::globalInstance()->start(QRunnable::create([self, pins, generation] {
    platform::RecycleBinInfo info;
    try {
      info = pins->recycleBinInfo();
    } catch (...) {
    }
    if (!self) return;
    QMetaObject::invokeMethod(
        self.data(),
        [self, info, generation] {
          if (!self || self->generation_ != generation) return;
          self->pending_ = false;
          self->apply(info);
        },
        Qt::QueuedConnection);
  }));
}

void RecycleBinMonitor::apply(const platform::RecycleBinInfo& info) {
  if (info.numItems == last_.numItems && info.sizeBytes == last_.sizeBytes) return;
  last_ = info;
  emit binUpdated(info.numItems, info.sizeBytes);
}

}  // namespace yasb::widgets

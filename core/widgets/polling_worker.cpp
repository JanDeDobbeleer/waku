#include "core/widgets/polling_worker.h"

namespace yasb::widgets {

PollingWorker::PollingWorker(PollFn poll, int intervalMs, QObject* parent)
    : QObject(parent), poll_(std::move(poll)), interval_(intervalMs) {}

PollingWorker::~PollingWorker() { stop(); }

void PollingWorker::start(bool pollImmediately) {
  if (thread_.isRunning()) return;
  timer_ = new QTimer;
  timer_->setInterval(interval_);
  timer_->moveToThread(&thread_);
  connect(timer_, &QTimer::timeout, timer_, [this] { emit dataReady(poll_()); });
  connect(&thread_, &QThread::started, timer_, [this, pollImmediately] {
    if (pollImmediately) emit dataReady(poll_());
    timer_->start();
  });
  connect(&thread_, &QThread::finished, timer_, &QObject::deleteLater);
  thread_.start();
}

void PollingWorker::stop() {
  if (!thread_.isRunning()) return;
  thread_.quit();
  thread_.wait();
  timer_ = nullptr;
}

}  // namespace yasb::widgets

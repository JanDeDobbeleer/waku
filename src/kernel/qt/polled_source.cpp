#include "kernel/qt/polled_source.h"

#include <algorithm>

namespace waku::kernel {

PolledSource::PolledSource(QObject* parent) : QObject(parent) {
  connect(&timer_, &QTimer::timeout, this, [this] { sample(); });
}

void PolledSource::setInterval(int ms) {
  ms = std::max(ms, 100);
  if (ms == interval_) return;
  interval_ = ms;
  restart();
  emit intervalChanged();
}

void PolledSource::setHistorySize(int size) {
  size = std::clamp(size, 2, 3600);
  if (size == historySize_) return;
  historySize_ = size;
  while (static_cast<int>(history_.size()) > historySize_) history_.pop_front();
  emit historySizeChanged();
}

QVariantList PolledSource::history() const {
  QVariantList list;
  for (double v : history_) list << v;
  return list;
}

void PolledSource::recordHistory(double value) {
  history_.push_back(value);
  while (static_cast<int>(history_.size()) > historySize_) history_.pop_front();
}

void PolledSource::setActive(bool active) {
  if (active == active_) return;
  active_ = active;
  restart();
  emit activeChanged();
}

void PolledSource::begin() {
  begun_ = true;
  restart();
}

void PolledSource::restart() {
  if (!begun_ || !active_) {
    timer_.stop();
    return;
  }
  timer_.start(interval_);
  sample();
}

void PolledSource::setReady() {
  if (ready_) return;
  ready_ = true;
  emit readyChanged();
}

}  // namespace waku::kernel

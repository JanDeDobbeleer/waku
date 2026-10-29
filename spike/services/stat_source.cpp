#include "spike/services/stat_source.h"

#include <algorithm>

namespace tidy::services {

StatSource::StatSource(QObject* parent) : QObject(parent) {
  connect(&timer_, &QTimer::timeout, this, [this] { sample(); });
}

void StatSource::setInterval(int ms) {
  ms = std::max(ms, 100);
  if (ms == interval_) return;
  interval_ = ms;
  restart();
  emit intervalChanged();
}

void StatSource::setHistorySize(int size) {
  size = std::clamp(size, 2, 3600);
  if (size == historySize_) return;
  historySize_ = size;
  while (static_cast<int>(history_.size()) > historySize_) history_.pop_front();
  emit historySizeChanged();
}

QVariantList StatSource::history() const {
  QVariantList list;
  for (double v : history_) list << v;
  return list;
}

void StatSource::recordHistory(double value) {
  history_.push_back(value);
  while (static_cast<int>(history_.size()) > historySize_) history_.pop_front();
}

void StatSource::setActive(bool active) {
  if (active == active_) return;
  active_ = active;
  restart();
  emit activeChanged();
}

void StatSource::componentComplete() {
  complete_ = true;
  restart();
}

void StatSource::restart() {
  if (!complete_ || !active_) {
    timer_.stop();
    return;
  }
  timer_.start(interval_);
  sample();
}

void StatSource::setReady() {
  if (ready_) return;
  ready_ = true;
  emit readyChanged();
}

}  // namespace tidy::services

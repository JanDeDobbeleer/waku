#include "spike/services/stat_source.h"

#include <algorithm>

namespace barnacle::services {

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

}  // namespace barnacle::services

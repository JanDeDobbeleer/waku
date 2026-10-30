#include "features/network/throughput.h"

namespace wabar::network {

void ThroughputTracker::update(const std::vector<InterfaceCounters>& counters,
                               double elapsedSeconds) {
  const bool timed = elapsedSeconds > 0;
  std::map<quint64, Previous> next;
  std::vector<AdapterRate> adapters;
  double rx = 0;
  double tx = 0;
  adapters.reserve(counters.size());
  for (const InterfaceCounters& now : counters) {
    quint64 rxDelta = 0;
    quint64 txDelta = 0;
    const auto before = previous_.find(now.id);
    if (before != previous_.end()) {
      // A smaller counter means the adapter was reset: it restarted at zero, so count what it
      // shows now.
      rxDelta = now.rxBytes >= before->second.rx ? now.rxBytes - before->second.rx : now.rxBytes;
      txDelta = now.txBytes >= before->second.tx ? now.txBytes - before->second.tx : now.txBytes;
    }
    next[now.id] = {now.rxBytes, now.txBytes};

    AdapterRate rate;
    rate.name = now.name;
    rate.isUp = now.isUp;
    if (timed) {
      rate.rxBytesPerSecond = static_cast<double>(rxDelta) / elapsedSeconds;
      rate.txBytesPerSecond = static_cast<double>(txDelta) / elapsedSeconds;
    }
    adapters.push_back(rate);
    if (now.isUp && timed) {
      rx += rate.rxBytesPerSecond;
      tx += rate.txBytesPerSecond;
      rxTotal_ += rxDelta;
      txTotal_ += txDelta;
    }
  }
  previous_ = std::move(next);
  adapters_ = std::move(adapters);
  rx_ = rx;
  tx_ = tx;
}

void ThroughputTracker::resetBaseline() { previous_.clear(); }

void ThroughputTracker::clearRates() {
  adapters_.clear();
  rx_ = 0;
  tx_ = 0;
}

}  // namespace wabar::network

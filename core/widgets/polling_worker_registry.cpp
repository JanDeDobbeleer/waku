#include "core/widgets/polling_worker_registry.h"

#include <QThread>

namespace yasb::widgets {

PollingWorker* PollingWorkerRegistry::get(const std::string& source, int intervalMs,
                                          std::function<PollingWorker::PollFn()> makePoll) {
  if (intervalMs <= 0 || !makePoll || QThread::currentThread() != thread()) return nullptr;

  const auto key = std::make_pair(source, intervalMs);
  if (const auto found = workers_.find(key); found != workers_.end() && found->second)
    return found->second;

  auto* worker = new PollingWorker(makePoll(), intervalMs, this);
  workers_[key] = worker;
  return worker;
}

void PollingWorkerRegistry::stopAll() {
  for (const auto& [key, worker] : workers_) {
    Q_UNUSED(key);
    if (worker) worker->stop();
  }
}

}  // namespace yasb::widgets

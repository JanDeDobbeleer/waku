#pragma once

#include <QObject>
#include <QPointer>
#include <map>
#include <string>
#include <utility>

#include "core/widgets/polling_worker.h"

namespace yasb::widgets {

class PollingWorkerRegistry final : public QObject {
  Q_OBJECT

 public:
  explicit PollingWorkerRegistry(QObject* parent = nullptr) : QObject(parent) {}

  PollingWorker* get(const std::string& source, int intervalMs,
                     std::function<PollingWorker::PollFn()> makePoll);
  void stopAll();
  std::size_t size() const { return workers_.size(); }

 private:
  std::map<std::pair<std::string, int>, QPointer<PollingWorker>> workers_;
};

}  // namespace yasb::widgets

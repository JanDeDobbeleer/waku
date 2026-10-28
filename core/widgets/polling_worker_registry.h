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

  // Shared services (one instance per key, e.g. "media"), owned by the registry and created on
  // first use by the GUI thread. Each is a QObject with an optional Q_INVOKABLE stop() that
  // stopAll() calls, so services that own a worker thread join it at shutdown.
  QObject* service(const std::string& key) const;
  void setService(const std::string& key, QObject* service);
  template <typename T, typename Make>
  T* service(const std::string& key, Make&& make) {
    if (auto* found = qobject_cast<T*>(service(key))) return found;
    T* created = make();
    setService(key, created);
    return created;
  }

 private:
  std::map<std::pair<std::string, int>, QPointer<PollingWorker>> workers_;
  std::map<std::string, QPointer<QObject>> services_;
};

}  // namespace yasb::widgets

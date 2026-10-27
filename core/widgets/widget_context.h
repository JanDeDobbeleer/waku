#pragma once

#include <QString>
#include <nlohmann/json.hpp>
#include <string>

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

class EventBus;
class PollingWorkerRegistry;

struct WidgetContext {
  std::string name;
  std::string type;
  nlohmann::json options;
  QString screenName;
  QString barId;
  EventBus& eventBus;
  bar::GlobalState& globalState;
  PollingWorkerRegistry& pollingWorkers;
};

}  // namespace yasb::widgets

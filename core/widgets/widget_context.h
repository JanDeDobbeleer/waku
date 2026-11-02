#pragma once

#include <QString>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

#include "core/config/options/battery.h"
#include "core/config/options/cpu.h"
#include "core/config/options/github.h"
#include "core/config/options/komorebi_active_layout.h"
#include "core/config/options/komorebi_workspaces.h"
#include "core/config/options/memory.h"

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

class EventBus;
class PollingWorkerRegistry;

using TypedWidgetOptions =
    std::variant<std::monostate, config::options::CpuConfig, config::options::MemoryConfig,
                 config::options::BatteryConfig, config::options::GithubConfig,
                 config::options::KomorebiWorkspacesConfig, config::options::ActiveLayoutConfig>;

struct WidgetContext {
  std::string name;
  std::string type;
  nlohmann::json options;
  QString screenName;
  QString barId;
  EventBus& eventBus;
  bar::GlobalState& globalState;
  PollingWorkerRegistry& pollingWorkers;
  TypedWidgetOptions typedOptions;
};

}  // namespace yasb::widgets

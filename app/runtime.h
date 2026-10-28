#pragma once

#include <memory>
#include <nlohmann/json.hpp>

#include "app/app_services.h"
#include "core/bar/theme_class.h"
#include "core/css/transition_engine.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "widgets/glazewm/glazewm_service.h"
#include "widgets/komorebi/komorebi_service.h"

namespace yasb::app {

class Runtime final {
 public:
  Runtime();
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  bool startServices(const nlohmann::json& config, bool restartWait);
  void stop();

  css::TransitionEngine& transitionEngine() { return transitionEngine_; }
  widgets::WidgetRegistry& widgetRegistry() { return widgetRegistry_; }
  widgets::EventBus& eventBus() { return eventBus_; }
  bar::GlobalState& globalState() { return globalState_; }
  widgets::PollingWorkerRegistry& pollingWorkers() { return pollingWorkers_; }
  AppServices* appServices() const { return appServices_.get(); }

 private:
  css::TransitionEngine transitionEngine_;
  widgets::WidgetRegistry widgetRegistry_;
  widgets::EventBus eventBus_;
  // The only komorebi client: owns the pipe listener thread (stopped by stop()).
  widgets::komorebi::KomorebiService komorebiService_{eventBus_};
  // The only GlazeWM client: owns the WebSocket worker thread (stopped by stop()).
  widgets::glazewm::GlazewmService glazewmService_{eventBus_};
  bar::GlobalState globalState_;
  widgets::PollingWorkerRegistry pollingWorkers_;
  std::unique_ptr<AppServices> appServices_;
  bool servicesStarted_ = false;
};

}  // namespace yasb::app

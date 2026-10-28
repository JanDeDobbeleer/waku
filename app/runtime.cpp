#include "app/runtime.h"

#include "widgets/komorebi/komorebi_widgets.h"
#include "widgets/stat_widgets.h"

namespace yasb::app {

Runtime::Runtime() {
  widgets::registerStatWidgets(widgetRegistry_);
  widgets::registerKomorebiWidgets(widgetRegistry_);
}

Runtime::~Runtime() { stop(); }

bool Runtime::startServices(const nlohmann::json& config, bool restartWait) {
  if (appServices_) return servicesStarted_;
  appServices_ = std::make_unique<AppServices>(config, eventBus_);
  servicesStarted_ = appServices_->start(restartWait);
  return servicesStarted_;
}

void Runtime::stop() {
  if (appServices_) {
    appServices_->stop();
    appServices_.reset();
  }
  servicesStarted_ = false;
  komorebiService_.stop();
  pollingWorkers_.stopAll();
}

}  // namespace yasb::app

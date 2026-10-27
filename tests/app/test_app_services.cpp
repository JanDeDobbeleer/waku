#include <gtest/gtest.h>

#include "app/app_services.h"
#include "core/widgets/event_bus.h"

TEST(AppServices, StopBeforeStartIsSafeAndIdempotent) {
  yasb::widgets::EventBus eventBus;
  yasb::app::AppServices services(nlohmann::json::object(), eventBus);

  services.stop();
  services.stop();
}

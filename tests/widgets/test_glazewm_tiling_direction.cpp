#include <gtest/gtest.h>

#include <QApplication>
#include <QPushButton>
#include <QVariantMap>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "core/bar/theme_class.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "widgets/glazewm/glazewm_service.h"
#include "widgets/glazewm/glazewm_widgets.h"
#include "widgets/glazewm/tiling_direction_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
namespace glz = yasb::widgets::glazewm;

namespace {

json snapshot(const GlazewmTilingDirectionWidget& widget) {
  const QPushButton* button = widget.button();
  return {{"button",
           {{"class", button->property("class").toString().toStdString()},
            {"hidden", button->isHidden()},
            {"text", button->text().toStdString()}}},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"widget_hidden", widget.isHidden()}};
}

json loadVectors() {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/glazewm_tiling_direction.json",
                   std::ios::binary);
  EXPECT_TRUE(in.good());
  return json::parse(in);
}

// Records the GlazeWM commands a widget publishes on the bus.
class CommandRecorder {
 public:
  explicit CommandRecorder(EventBus& bus) {
    QObject::connect(&bus, &EventBus::event, &receiver_,
                     [this](const QString& name, const QVariantMap& payload) {
                       if (name != glz::kCommandEvent) return;
                       commands_.push_back(payload.value("command").toString().toStdString());
                     });
  }
  json take() {
    json result = commands_;
    commands_ = json::array();
    return result;
  }

 private:
  QObject receiver_;
  json commands_ = json::array();
};

}  // namespace

class GlazewmTilingDirectionTest : public testing::Test {
 protected:
  WidgetContext context(EventBus& bus, const json& options, const json& normalized) {
    return {"glazewm_tiling_direction",
            "glazewm.tiling_direction.GlazewmTilingDirectionWidget",
            options,
            {},
            {},
            bus,
            globalState_,
            pollingWorkers_,
            normalized.get<yasb::config::options::GlazewmTilingDirectionConfig>()};
  }

  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

TEST_F(GlazewmTilingDirectionTest, ReplaysPythonVectors) {
  const json cases = loadVectors();
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    EventBus bus;
    CommandRecorder recorder(bus);
    GlazewmTilingDirectionWidget widget(context(bus, c.at("options"), c.at("normalized_options")),
                                        /*subscribe=*/false);
    widget.show();
    EXPECT_EQ(snapshot(widget), c.at("initial")) << snapshot(widget).dump(1);
    recorder.take();

    size_t index = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.dump());
      if (step.contains("connected")) {
        widget.onConnection(step.at("connected").get<bool>());
      } else if (step.contains("direction")) {
        widget.onTilingDirection(step.at("direction") == "vertical"
                                     ? glz::TilingDirection::Vertical
                                     : glz::TilingDirection::Horizontal);
      } else if (step.contains("click")) {
        widget.button()->click();
      } else {
        FAIL() << "unknown step";
      }
      EXPECT_EQ(snapshot(widget), s.at("snapshot")) << snapshot(widget).dump(1);
      EXPECT_EQ(recorder.take(), s.at("commands"));
    }
  }
}

TEST_F(GlazewmTilingDirectionTest, RegisteredAndDrivenThroughTheBus) {
  WidgetRegistry registry;
  registerGlazewmWidgets(registry);
  EXPECT_TRUE(registry.contains("glazewm.tiling_direction.GlazewmTilingDirectionWidget"));

  const json c = loadVectors().at(0);
  EventBus bus;
  QVariantMap subscription;
  QObject::connect(&bus, &EventBus::event, [&](const QString& name, const QVariantMap& payload) {
    if (name == glz::kSubscribeEvent) subscription = payload;
  });
  GlazewmTilingDirectionWidget widget(context(bus, c.at("options"), c.at("normalized_options")));
  EXPECT_EQ(subscription.value("topic").toString(), glz::kTopicTilingDirection);
  EXPECT_EQ(subscription.value("uri").toString(), QStringLiteral("ws://localhost:6123"));
  EXPECT_TRUE(widget.button()->isHidden());

  emit bus.event(glz::kStateEvent, {{"kind", "connection"}, {"connected", true}});
  EXPECT_FALSE(widget.button()->isHidden());
  emit bus.event(glz::kStateEvent,
                 {{"kind", "tiling_direction"},
                  {"direction", static_cast<int>(glz::TilingDirection::Vertical)}});
  EXPECT_EQ(widget.button()->text(), QString::fromUcs4(U"\U000F04E2"));  // schema default
  emit bus.event(glz::kStateEvent, {{"kind", "connection"}, {"connected", false}});
  EXPECT_TRUE(widget.button()->isHidden());
}

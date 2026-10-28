#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QWidget>
#include <algorithm>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "core/bar/theme_class.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "widgets/glazewm/binding_mode_widget.h"
#include "widgets/glazewm/glazewm_service.h"
#include "widgets/glazewm/glazewm_widgets.h"

using nlohmann::json;
using namespace yasb::widgets;
namespace gw = yasb::widgets::glazewm;

namespace {

json labelState(const QLabel* label) {
  return {{"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()},
          {"text", label->text().toStdString()}};
}

json snapshot(GlazewmBindingModeWidget& widget) {
  json primary = json::array();
  json alt = json::array();
  for (const QLabel* label : widget.labels()) primary.push_back(labelState(label));
  for (const QLabel* label : widget.labelsAlt()) alt.push_back(labelState(label));
  json order = json::array();
  const QLayout* layout = widget.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const QWidget* item = layout->itemAt(i)->widget();
    const auto isIn = [&](const std::vector<QLabel*>& list) {
      return std::find(list.begin(), list.end(), item) != list.end();
    };
    order.push_back(isIn(widget.labels()) ? "primary" : isIn(widget.labelsAlt()) ? "alt" : "other");
  }
  return {{"alt", alt},
          {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"layout_order", order},
          {"primary", primary},
          {"widget_hidden", widget.isHidden()}};
}

// Records the glazewm commands a widget publishes on the bus.
class CommandRecorder {
 public:
  explicit CommandRecorder(EventBus& bus) {
    QObject::connect(&bus, &EventBus::event, &receiver_,
                     [this](const QString& name, const QVariantMap& payload) {
                       if (name == gw::kCommandEvent) {
                         commands_.push_back(payload.value("command").toString().toStdString());
                       }
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

json loadVectors() {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/glazewm_binding_mode.json",
                   std::ios::binary);
  EXPECT_TRUE(in.good());
  return json::parse(in);
}

gw::BindingMode modeFrom(const json& mode) {
  gw::BindingMode result;
  if (mode.contains("name") && mode.at("name").is_string()) {
    result.name = QString::fromStdString(mode.at("name").get<std::string>());
  }
  if (mode.contains("display_name") && mode.at("display_name").is_string()) {
    result.displayName = QString::fromStdString(mode.at("display_name").get<std::string>());
  }
  return result;
}

class GlazewmBindingModeTest : public testing::Test {
 protected:
  WidgetContext context(const json& options, EventBus& bus) {
    return {"glazewm_binding_mode",
            "glazewm.binding_mode.GlazewmBindingModeWidget",
            options,
            {},
            {},
            bus,
            globalState_,
            pollingWorkers_,
            options.get<yasb::config::options::GlazewmBindingModeConfig>()};
  }

  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

}  // namespace

TEST_F(GlazewmBindingModeTest, ReplaysPythonVectors) {
  const json cases = loadVectors();
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    EventBus bus;
    CommandRecorder recorder(bus);
    // The bar parents widgets; a child host keeps show() from opening a top-level window.
    QWidget host;
    GlazewmBindingModeWidget widget(context(c.at("normalized_options"), bus), /*subscribe=*/false);
    widget.setParent(&host);
    widget.show();
    EXPECT_EQ(snapshot(widget), c.at("initial")) << snapshot(widget).dump(1);
    recorder.take();

    size_t index = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.dump());
      if (step.contains("connection")) {
        widget.onConnectionStatus(step.at("connection").get<bool>());
      } else if (step.contains("mode")) {
        widget.onBindingMode(modeFrom(step.at("mode")));
      } else if (step.contains("callback")) {
        widget.runCallback(QString::fromStdString(step.at("callback").get<std::string>()));
      } else {
        FAIL() << "unknown step";
      }
      EXPECT_EQ(snapshot(widget), s.at("snapshot")) << snapshot(widget).dump(1);
      EXPECT_EQ(recorder.take(), s.at("commands"));
    }
  }
}

TEST_F(GlazewmBindingModeTest, RegisteredAndDrivenThroughTheBus) {
  WidgetRegistry registry;
  registerGlazewmWidgets(registry);
  EXPECT_TRUE(registry.contains("glazewm.binding_mode.GlazewmBindingModeWidget"));

  const json options = loadVectors().at(0).at("normalized_options");
  EventBus bus;
  QString topic;
  QString uri;
  QObject::connect(&bus, &EventBus::event, [&](const QString& name, const QVariantMap& payload) {
    if (name == gw::kSubscribeEvent) {
      topic = payload.value("topic").toString();
      uri = payload.value("uri").toString();
    }
  });
  CommandRecorder recorder(bus);
  QWidget host;
  GlazewmBindingModeWidget widget(context(options, bus));
  widget.setParent(&host);
  EXPECT_EQ(topic, gw::kTopicBindingMode);
  EXPECT_EQ(uri, QString::fromStdString(options.at("glazewm_server_uri").get<std::string>()));
  EXPECT_TRUE(widget.isHidden());

  gw::BindingMode resize;
  resize.name = QStringLiteral("resize");
  emit bus.event(gw::kStateEvent,
                 {{"kind", gw::kTopicBindingMode},
                  {"bindingMode", QVariant::fromValue<gw::BindingModePtr>(
                                      std::make_shared<const gw::BindingMode>(resize))}});
  EXPECT_FALSE(widget.isHidden());
  EXPECT_EQ(widget.labels().at(1)->text(), QStringLiteral("resize"));

  widget.runCallback(QStringLiteral("disable_binding_mode"));
  EXPECT_EQ(recorder.take(), json::array({"command wm-disable-binding-mode --name resize"}));

  emit bus.event(gw::kStateEvent, {{"kind", "connection"}, {"connected", false}});
  EXPECT_TRUE(widget.isHidden());
}

TEST_F(GlazewmBindingModeTest, ParentlessWidgetStaysHiddenUntilAdopted) {
  const json options = loadVectors().at(0).at("normalized_options");
  EventBus bus;
  QWidget host;  // declared first: it must outlive the widget it adopts
  GlazewmBindingModeWidget widget(context(options, bus), /*subscribe=*/false);
  gw::BindingMode resize;
  resize.name = QStringLiteral("resize");
  widget.onBindingMode(resize);
  EXPECT_TRUE(widget.isHidden());
  widget.setParent(&host);
  EXPECT_FALSE(widget.isHidden());
}

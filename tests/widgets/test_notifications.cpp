#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QSignalSpy>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/bar/theme_class.h"
#include "core/config/loader.h"
#include "core/widgets/backdrop.h"
#include "core/widgets/callbacks.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "platform/notifications.h"
#include "widgets/notifications/notifications_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

class FakeSource final : public NotificationSource {
 public:
  void start(NotificationSink* sink) override { sink_ = sink; }
  void stop() override { sink_ = nullptr; }
  // The real source reports the new count through WNF once the toasts are gone.
  void clearAll() override {
    ++clears;
    if (sink_) sink_->countChanged(0);
  }
  NotificationSink* sink_ = nullptr;
  int clears = 0;
};

yasb::config::options::NotificationsConfig notificationOptions(const json& options) {
  const json config = {
      {"bars", {{"main", {{"widgets", {{"left", {"notifications"}}}}}}}},
      {"widgets",
       {{"notifications",
         {{"type", "yasb.notifications.NotificationsWidget"}, {"options", options}}}}}};
  const auto loaded = yasb::config::loadConfigText(config.dump());
  EXPECT_TRUE(loaded.ok()) << loaded.message;
  auto typed = loaded.widgetOptions<yasb::config::options::NotificationsConfig>("notifications");
  EXPECT_TRUE(typed.has_value());
  return typed.value_or(yasb::config::options::NotificationsConfig{});
}

json userOptions() {
  return {{"label", "<span>{icon}</span>"},
          {"label_alt", "{count} notifications"},
          {"hide_empty", false},
          {"tooltip", false},
          {"icons", {{"new", "\xEE\xB4\x8C"}, {"default", "\xEE\xA4\x9C"}}},
          {"callbacks",
           {{"on_left", "toggle_notification"},
            {"on_right", "clear_notifications"},
            {"on_middle", "do_nothing"}}}};
}

class NotificationsTest : public testing::Test {
 protected:
  void SetUp() override {
    auto source = std::make_unique<FakeSource>();
    source_ = source.get();
    service_ = std::make_unique<NotificationService>(std::move(source));
    service_->start();
  }

  void TearDown() override {
    widget_.reset();
    service_.reset();
  }

  NotificationsWidget& build(const json& options) {
    TypedWidgetOptions typed = notificationOptions(options);
    WidgetContext context{"notifications",
                          "yasb.notifications.NotificationsWidget",
                          options,
                          {},
                          {},
                          eventBus_,
                          globalState_,
                          pollingWorkers_,
                          std::move(typed)};
    widget_.reset(new NotificationsWidget(context, service_.get()));
    return *widget_;
  }

  // The WNF callback path: the sink posts to the service, which emits countChanged.
  void sendCount(unsigned count) {
    source_->sink_->countChanged(count);
    QCoreApplication::processEvents();
  }

  FakeSource* source_ = nullptr;
  std::unique_ptr<NotificationService> service_;
  std::unique_ptr<NotificationsWidget> widget_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

json labelJson(const QLabel* label) {
  return {{"text", label->text().toStdString()},
          {"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()}};
}

json snapshot(NotificationsWidget& widget) {
  json primary = json::array();
  json alt = json::array();
  for (const QLabel* label : widget.labels()) primary.push_back(labelJson(label));
  for (const QLabel* label : widget.labelsAlt()) alt.push_back(labelJson(label));
  json order = json::array();
  QLayout* layout = widget.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const QWidget* child = layout->itemAt(i)->widget();
    const auto in = [&](const std::vector<QLabel*>& group) {
      return std::find(group.begin(), group.end(), child) != group.end();
    };
    order.push_back(in(widget.labels()) ? "primary" : in(widget.labelsAlt()) ? "alt" : "other");
  }
  return {{"widget_hidden", widget.isHidden()},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"primary", primary},
          {"alt", alt},
          {"layout_order", order}};
}

}  // namespace

TEST_F(NotificationsTest, ReplaysPythonLabelVectors) {
  std::ifstream in(std::filesystem::path(YASB_VECTORS_DIR) / "labels" / "notifications.json",
                   std::ios::binary);
  const json vectors = json::parse(in);
  ASSERT_FALSE(vectors.empty());
  for (const json& scenario : vectors) {
    SCOPED_TRACE(scenario["scenario"].get<std::string>());
    TearDown();  // fresh service per scenario
    SetUp();
    NotificationsWidget& widget = build(scenario["options"]);
    widget.show();
    EXPECT_EQ(snapshot(widget), scenario["initial"]) << "initial";
    int index = 0;
    for (const json& step : scenario["steps"]) {
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step["step"].dump());
      const json& what = step["step"];
      if (what.contains("toggle")) {
        widget.runCallback("toggle_label");
      } else {
        sendCount(what["count"].get<unsigned>());
      }
      EXPECT_EQ(snapshot(widget), step["snapshot"]);
    }
  }
}

TEST_F(NotificationsTest, NewNotificationSwitchesIconAndClearReturnsToDefault) {
  NotificationsWidget& widget = build(userOptions());
  widget.show();
  EXPECT_EQ(widget.labels()[0]->text(), QString::fromUtf8("\xEE\xA4\x9C"));
  sendCount(3);
  EXPECT_EQ(widget.count(), 3);
  EXPECT_EQ(widget.labels()[0]->text(), QString::fromUtf8("\xEE\xB4\x8C"));
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "icon new-notification");

  // The configured on_right callback clears everything and the WNF count drops to 0.
  EXPECT_TRUE(widget.runCallback("clear_notifications"));
  QCoreApplication::processEvents();
  EXPECT_EQ(source_->clears, 1);
  EXPECT_EQ(widget.count(), 0);
  EXPECT_EQ(widget.labels()[0]->text(), QString::fromUtf8("\xEE\xA4\x9C"));
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "icon");
}

TEST_F(NotificationsTest, CountIsCappedInTheAltLabel) {
  NotificationsWidget& widget = build(userOptions());
  widget.show();
  widget.runCallback("toggle_label");
  sendCount(9);
  EXPECT_EQ(widget.labelsAlt()[0]->text(), "9 notifications");
  sendCount(10);
  EXPECT_EQ(widget.labelsAlt()[0]->text(), "9+ notifications");
}

TEST_F(NotificationsTest, ToggleNotificationRunsTheSystemFunctionForThisWindowsBuild) {
  const auto saved = systemFunctions();
  QStringList called;
  systemFunctions().clear();
  systemFunctions()["notification_center"] = [&] { called << "notification_center"; };
  systemFunctions()["quick_settings"] = [&] { called << "quick_settings"; };
  NotificationsWidget& widget = build(userOptions());
  EXPECT_TRUE(widget.runCallback("toggle_notification"));
  const unsigned build = yasb::widgets::windowsBuild();
  const QString expected = build != 0 && build < 22000 ? "quick_settings" : "notification_center";
  EXPECT_EQ(NotificationsWidget::notificationCenterFunction(), expected);
  EXPECT_EQ(called, QStringList{expected});
  systemFunctions() = saved;
}

TEST_F(NotificationsTest, ToggleNotificationWithoutRegisteredFunctionDoesNotThrow) {
  const auto saved = systemFunctions();
  systemFunctions().clear();
  NotificationsWidget& widget = build(userOptions());
  EXPECT_TRUE(widget.runCallback("toggle_notification"));
  systemFunctions() = saved;
}

TEST_F(NotificationsTest, HideEmptyShowsInsideAParentOnlyWhileThereAreNotifications) {
  QWidget parent;
  json options = userOptions();
  options["hide_empty"] = true;
  NotificationsWidget& widget = build(options);
  widget.setParent(&parent);
  parent.show();
  EXPECT_TRUE(widget.isHidden());
  sendCount(2);
  EXPECT_FALSE(widget.isHidden());
  sendCount(0);
  EXPECT_TRUE(widget.isHidden());
  widget.setParent(nullptr);  // widget_ owns it
}

TEST_F(NotificationsTest, WidgetsShareTheServiceCount) {
  sendCount(4);
  NotificationsWidget& widget = build(userOptions());
  EXPECT_EQ(widget.count(), 4);
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "icon new-notification");
}

TEST_F(NotificationsTest, ServiceEmitsOnlyWhenTheCountChanges) {
  QSignalSpy spy(service_.get(), &NotificationService::countChanged);
  sendCount(2);
  sendCount(2);
  sendCount(3);
  EXPECT_EQ(spy.count(), 2);
  EXPECT_EQ(service_->count(), 3);
}

TEST_F(NotificationsTest, ClearAfterStopIsIgnored) {
  sendCount(2);
  service_->stop();
  service_->clearAll();
  EXPECT_EQ(source_->clears, 0);
  EXPECT_EQ(service_->count(), 2);
}

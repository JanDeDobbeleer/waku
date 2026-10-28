#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPoint>
#include <QTest>
#include <QWheelEvent>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "core/bar/theme_class.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/label_format.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "platform/window_system.h"
#include "widgets/glazewm/glazewm_service.h"
#include "widgets/glazewm/glazewm_state.h"
#include "widgets/glazewm/glazewm_widgets.h"
#include "widgets/glazewm/workspaces_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
namespace gw = yasb::widgets::glazewm;
namespace platform = yasb::platform;
using yasb::config::options::GlazewmWorkspacesConfig;

namespace {

constexpr long long kMonitor = 65537;  // MONITOR in tools/parity/vec_glazewm_workspaces.py
constexpr platform::WindowHandle kNoIconHwnd = 702;  // NO_ICON_HWND in the exporter

json loadVectors(const std::string& file) {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/" + file, std::ios::binary);
  EXPECT_TRUE(in.good()) << file;
  return json::parse(in);
}

// A window system that answers icon and pid queries like the exporter's stubs: pid is hwnd / 2 and
// handle 702 has no icon.
class IconSystem final : public platform::WindowSystem {
 public:
  bool isWindow(platform::WindowHandle) const override { return true; }
  bool isIconic(platform::WindowHandle) const override { return false; }
  platform::WindowHandle foregroundWindow() const override { return 0; }
  std::vector<platform::WindowHandle> enumerateWindows() const override { return {}; }
  platform::WindowInfo query(platform::WindowHandle hwnd) const override {
    platform::WindowInfo info;
    info.hwnd = hwnd;
    info.pid = static_cast<std::uint32_t>(hwnd / 2);
    return info;
  }
  bool isTaskbarWindow(const platform::WindowInfo&) const override { return true; }
  bool canMinimize(platform::WindowHandle) const override { return true; }
  platform::WindowHandle monitorOfWindow(platform::WindowHandle) const override { return 0; }
  QRect windowRect(platform::WindowHandle) const override { return {}; }
  std::pair<platform::WindowHandle, platform::WindowHandle> resolveBaseAndFocus(
      platform::WindowHandle hwnd) const override {
    return {hwnd, hwnd};
  }
  bool isOwnerRootActive(platform::WindowHandle) const override { return false; }
  std::optional<QString> appUniqueId(const platform::WindowInfo&) const override {
    return std::nullopt;
  }
  platform::IconBitmap windowIcon(platform::WindowHandle hwnd, int size) const override {
    platform::IconBitmap bitmap;
    if (hwnd == kNoIconHwnd || noIcon.count(hwnd)) return bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, '\x80');
    return bitmap;
  }
  void restoreWindow(platform::WindowHandle) override {}
  void minimizeWindow(platform::WindowHandle) override {}
  void showWindow(platform::WindowHandle) override {}
  void setForeground(platform::WindowHandle) override {}
  void closeApplication(platform::WindowHandle, bool) override {}

  std::set<platform::WindowHandle> noIcon;
};

// Records the GlazeWM command texts a widget publishes on the bus.
class CommandRecorder {
 public:
  explicit CommandRecorder(EventBus& bus) {
    QObject::connect(&bus, &EventBus::event, &receiver_,
                     [this](const QString& name, const QVariantMap& payload) {
                       if (name != gw::kCommandEvent) return;
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

json labelState(const QLabel* label) {
  return {{"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()},
          {"text", label->text().toStdString()}};
}

json snapshot(GlazewmWorkspacesWidget& widget) {
  json buttons = json::array();
  for (GlazewmWorkspaceButtonBase* button : widget.layoutButtons()) {
    json entry = {{"name", button->name.toStdString()},
                  {"class", button->widget()->property("class").toString().toStdString()},
                  {"hidden", button->widget()->isHidden()},
                  {"text", button->labelText().toStdString()}};
    if (const auto* icons = dynamic_cast<const GlazewmIconButton*>(button)) {
      json classes = json::array();
      for (const QLabel* label : icons->iconLabels()) {
        classes.push_back(label->property("class").toString().toStdString());
      }
      entry["kind"] = "icons";
      entry["label_hidden"] = icons->textLabel()->isHidden();
      entry["icon_classes"] = classes;
    } else {
      entry["kind"] = "button";
    }
    buttons.push_back(entry);
  }
  return {{"button_count", widget.layoutButtons().size()},
          {"buttons", buttons},
          {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
          {"container_hidden", widget.labelContainer()->isHidden()},
          {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
          {"offline", labelState(widget.offlineLabel())},
          {"widget_hidden", widget.isHidden()}};
}

gw::MonitorsPtr monitorsFrom(const json& raw) {
  auto parsed = gw::parseMonitors(json{{"monitors", raw}});
  if (!parsed) return {};
  return std::make_shared<const std::vector<gw::Monitor>>(std::move(*parsed));
}

}  // namespace

class GlazewmWorkspacesTest : public testing::Test {
 protected:
  WidgetContext context(EventBus& bus, GlazewmWorkspacesConfig options) {
    return {"glazewm_workspaces",
            "glazewm.workspaces.GlazewmWorkspacesWidget",
            json::object(),
            {},
            {},
            bus,
            globalState_,
            pollingWorkers_,
            std::move(options)};
  }

  std::unique_ptr<GlazewmWorkspacesWidget> make(EventBus& bus,
                                                const GlazewmWorkspacesConfig& options,
                                                std::shared_ptr<IconSystem> system = nullptr) {
    ctx_ = std::make_unique<WidgetContext>(context(bus, options));
    auto widget = std::make_unique<GlazewmWorkspacesWidget>(*ctx_);
    widget->setMonitorResolver([] { return std::optional<long long>(kMonitor); });
    widget->setWindowSystem(system ? system : std::make_shared<IconSystem>(), /*async=*/false);
    return widget;
  }

  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
  std::unique_ptr<WidgetContext> ctx_;
};

// --- parity vectors --------------------------------------------------------------------------

TEST_F(GlazewmWorkspacesTest, ReplayPythonVectors) {
  const json cases = loadVectors("glazewm_workspaces.json");
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    EventBus bus;
    CommandRecorder recorder(bus);
    auto widget = make(bus, c.at("normalized_options").get<GlazewmWorkspacesConfig>());
    widget->show();
    EXPECT_EQ(snapshot(*widget), c.at("initial")) << snapshot(*widget).dump(1);
    recorder.take();

    size_t index = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(index++) + " " + step.dump().substr(0, 120));
      if (step.contains("connected")) {
        widget->onConnection(step.at("connected").get<bool>());
      } else if (step.contains("monitors")) {
        widget->onWorkspaces(monitorsFrom(step.at("monitors")));
      } else if (step.contains("click")) {
        GlazewmWorkspaceButtonBase* button =
            widget->buttonByName(QString::fromStdString(step.at("click").get<std::string>()));
        ASSERT_NE(button, nullptr);
        button->activate();
      } else if (step.contains("wheel")) {
        widget->scrollWorkspaces(step.at("wheel").get<int>());
      } else {
        FAIL() << "unknown step";
      }
      EXPECT_EQ(snapshot(*widget), s.at("snapshot")) << snapshot(*widget).dump(1);
      EXPECT_EQ(recorder.take(), s.at("commands"));
    }
  }
}

// --- extra C++ tests ------------------------------------------------------------------------

TEST(GlazewmNaturalSort, DigitRunsAreNumericAndTextIsLowercase) {
  std::vector<QString> names = {
      QStringLiteral("a10"),  QStringLiteral("b"),   QStringLiteral("A"),   QStringLiteral("a2"),
      QStringLiteral("10"),   QStringLiteral("2"),   QStringLiteral("1"),   QStringLiteral("a1"),
      QStringLiteral("1-10"), QStringLiteral("1-2"), QStringLiteral("007"), QStringLiteral("7")};
  std::stable_sort(names.begin(), names.end(), glazewmNaturalLess);
  std::vector<std::string> sorted;
  for (const QString& name : names) sorted.push_back(name.toStdString());
  EXPECT_EQ(sorted, (std::vector<std::string>{"1", "1-2", "1-10", "2", "007", "7", "10", "A", "a1",
                                              "a2", "a10", "b"}));
  EXPECT_FALSE(glazewmNaturalLess(QStringLiteral("A"), QStringLiteral("a")));
  EXPECT_FALSE(glazewmNaturalLess(QStringLiteral("a"), QStringLiteral("A")));
  EXPECT_TRUE(glazewmNaturalLess(QStringLiteral("99999999999999999999"),
                                 QStringLiteral("100000000000000000000")));
}

TEST_F(GlazewmWorkspacesTest, RegistersAndFollowsBusEvents) {
  WidgetRegistry registry;
  registerGlazewmWidgets(registry);
  EXPECT_TRUE(registry.contains("glazewm.workspaces.GlazewmWorkspacesWidget"));

  EventBus bus;
  CommandRecorder recorder(bus);
  QStringList subscriptions;
  QObject::connect(&bus, &EventBus::event, [&](const QString& name, const QVariantMap& payload) {
    if (name == gw::kSubscribeEvent) {
      subscriptions << payload.value("topic").toString() + "|" + payload.value("uri").toString();
    }
  });
  GlazewmWorkspacesConfig options;
  options.glazewm_server_uri = "ws://localhost:6123";
  options.offline_label = "off";
  auto widget = make(bus, options);
  EXPECT_TRUE(subscriptions.isEmpty());  // subscribes when first shown
  widget->show();
  widget->show();
  EXPECT_EQ(subscriptions, QStringList{"workspaces|ws://localhost:6123"});
  EXPECT_EQ(widget->monitorHandle(), kMonitor);
  EXPECT_FALSE(widget->offlineLabel()->isHidden());
  EXPECT_TRUE(widget->labelContainer()->isHidden());

  emit bus.event(gw::kStateEvent, {{"kind", "connection"}, {"connected", true}});
  EXPECT_TRUE(widget->offlineLabel()->isHidden());
  EXPECT_FALSE(widget->labelContainer()->isHidden());

  const json raw = {{{"type", "monitor"},
                     {"handle", kMonitor},
                     {"children",
                      {{{"type", "workspace"},
                        {"name", "1"},
                        {"hasFocus", true},
                        {"isDisplayed", true},
                        {"children", json::array()}}}}}};
  emit bus.event(gw::kStateEvent,
                 {{"kind", "workspaces"}, {"monitors", QVariant::fromValue(monitorsFrom(raw))}});
  ASSERT_EQ(widget->layoutButtons().size(), 1u);
  EXPECT_EQ(widget->layoutButtons()[0]->status(), GlazewmWorkspaceStatus::FocusedEmpty);

  // A click on the button sends the focus command through the bus.
  recorder.take();
  widget->layoutButtons()[0]->activate();
  EXPECT_EQ(recorder.take(), json::array({"command focus --workspace 1"}));

  // A real left press on a plain button (QPushButton click) does the same.
  auto* button = dynamic_cast<QPushButton*>(widget->layoutButtons()[0]->widget());
  ASSERT_NE(button, nullptr);
  QTest::mouseClick(button, Qt::LeftButton);
  EXPECT_EQ(recorder.take(), json::array({"command focus --workspace 1"}));

  emit bus.event(gw::kStateEvent, {{"kind", "connection"}, {"connected", false}});
  EXPECT_FALSE(widget->offlineLabel()->isHidden());
  EXPECT_TRUE(widget->labelContainer()->isHidden());
}

TEST_F(GlazewmWorkspacesTest, MonitorUnknownIgnoresWorkspaces) {
  EventBus bus;
  auto widget = make(bus, GlazewmWorkspacesConfig{});
  widget->setMonitorResolver([]() -> std::optional<long long> { return std::nullopt; });
  widget->show();
  EXPECT_FALSE(widget->monitorHandle().has_value());
  const json raw = {{{"type", "monitor"}, {"handle", kMonitor}, {"children", json::array()}}};
  widget->onWorkspaces(monitorsFrom(raw));
  EXPECT_TRUE(widget->layoutButtons().empty());
}

TEST_F(GlazewmWorkspacesTest, BadLabelTemplateRaisesLikePython) {
  EventBus bus;
  GlazewmWorkspacesConfig options;
  options.populated_label = "{missing}";
  auto widget = make(bus, options);
  widget->show();
  const json raw = {
      {{"type", "monitor"},
       {"handle", kMonitor},
       {"children", {{{"type", "workspace"}, {"name", "1"}, {"children", json::array()}}}}}};
  EXPECT_THROW(widget->onWorkspaces(monitorsFrom(raw)), FormatError);
  // Through the bus the error is logged, never propagated.
  EXPECT_NO_THROW(emit bus.event(
      gw::kStateEvent,
      {{"kind", "workspaces"}, {"monitors", QVariant::fromValue(monitorsFrom(raw))}}));
}

namespace {

json window(long long hwnd, bool floating = false) {
  return {{"type", "window"},
          {"id", "w"},
          {"title", "t"},
          {"handle", hwnd},
          {"className", "C"},
          {"processName", "p"},
          {"displayState", "shown"},
          {"state", {{"type", floating ? "floating" : "tiling"}}}};
}

json workspace(const std::string& name, json children, bool focus = false, bool displayed = false) {
  return {{"type", "workspace"},
          {"name", name},
          {"hasFocus", focus},
          {"isDisplayed", displayed},
          {"children", std::move(children)}};
}

json oneMonitor(std::vector<json> workspaces) {
  return json::array({{{"type", "monitor"}, {"handle", kMonitor}, {"children", workspaces}}});
}

std::vector<std::string> iconClasses(const GlazewmWorkspaceButtonBase* button) {
  std::vector<std::string> classes;
  const auto* icons = dynamic_cast<const GlazewmIconButton*>(button);
  if (!icons) return classes;
  for (const QLabel* label : icons->iconLabels()) {
    classes.push_back(label->property("class").toString().toStdString());
  }
  return classes;
}

}  // namespace

TEST_F(GlazewmWorkspacesTest, IconRulesPerStatusAndEnabledFocusedQuirk) {
  struct Case {
    bool populated;
    bool active;
    std::optional<bool> focused;
    size_t focusedIcons;  // icons on the focused+populated workspace
    size_t activeIcons;   // icons on the active+populated workspace
    size_t plainIcons;    // icons on the populated workspace
  };
  // enabled_focused None follows enabled_active; an explicit value wins.
  const Case cases[] = {
      {true, false, std::nullopt, 0, 0, 2}, {false, true, std::nullopt, 2, 2, 0},
      {false, true, false, 0, 2, 0},        {false, false, true, 2, 0, 0},
      {true, true, false, 0, 2, 2},         {true, true, true, 2, 2, 2},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(std::to_string(c.populated) + std::to_string(c.active) +
                 (c.focused ? std::to_string(*c.focused) : "n"));
    EventBus bus;
    GlazewmWorkspacesConfig options;
    options.app_icons.enabled_populated = c.populated;
    options.app_icons.enabled_active = c.active;
    options.app_icons.enabled_focused = c.focused;
    auto widget = make(bus, options);
    widget->show();
    // "1" is focused and displayed, "3" is plain populated.
    widget->onWorkspaces(
        monitorsFrom(oneMonitor({workspace("1", {window(100), window(102)}, true, true),
                                 workspace("3", {window(120), window(122)})})));
    ASSERT_EQ(widget->layoutButtons().size(), 2u);
    EXPECT_EQ(widget->buttonByName("1")->status(), GlazewmWorkspaceStatus::FocusedPopulated);
    EXPECT_EQ(iconClasses(widget->buttonByName("1")).size(), c.focusedIcons);
    EXPECT_EQ(widget->buttonByName("3")->status(), GlazewmWorkspaceStatus::Populated);
    EXPECT_EQ(iconClasses(widget->buttonByName("3")).size(), c.plainIcons);
    // "2" is displayed here while the focus sits on another monitor: active, not focused.
    widget->onWorkspaces(
        monitorsFrom(json::array({{{"type", "monitor"},
                                   {"handle", kMonitor},
                                   {"children",
                                    {workspace("2", {window(110), window(112)}, false, true),
                                     workspace("3", {window(120), window(122)})}}},
                                  {{"type", "monitor"},
                                   {"handle", 1},
                                   {"children", {workspace("9", json::array(), true, true)}}}})));
    EXPECT_EQ(widget->buttonByName("2")->status(), GlazewmWorkspaceStatus::ActivePopulated);
    EXPECT_EQ(iconClasses(widget->buttonByName("2")).size(), c.activeIcons);
  }
}

TEST_F(GlazewmWorkspacesTest, IconsOptionsAndRefresh) {
  EventBus bus;
  GlazewmWorkspacesConfig options;
  options.app_icons.enabled_populated = true;
  options.app_icons.max_icons = 2;
  options.app_icons.hide_floating = true;
  options.app_icons.hide_label = true;
  options.app_icons.size = 20;
  auto system = std::make_shared<IconSystem>();
  auto widget = make(bus, options, system);
  widget->show();
  const json children = {window(100), window(102), window(104), window(500, true)};
  widget->onWorkspaces(monitorsFrom(oneMonitor({workspace("1", children)})));

  auto* button = dynamic_cast<GlazewmIconButton*>(widget->buttonByName("1"));
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(iconClasses(button), (std::vector<std::string>{"icon icon-1", "icon icon-2"}));
  EXPECT_TRUE(button->textLabel()->isHidden());  // hide_label with icons
  ASSERT_FALSE(button->iconLabels().empty());
  EXPECT_FALSE(button->iconLabels()[0]->pixmap().isNull());
  EXPECT_EQ(button->icons().size(), 3u);  // the floating window is skipped

  // The window with 102 loses its icon (None): the next one fills the slot.
  system->noIcon.insert(102);
  widget->setWindowSystem(system, /*async=*/false);
  button->refreshIcons();
  EXPECT_EQ(iconClasses(button).size(), 2u);
  ASSERT_EQ(button->icons().size(), 3u);
  EXPECT_TRUE(button->icons()[1].second.isNull());  // 102 has no icon (None)

  // No windows: the label is back and the icon labels go.
  widget->onWorkspaces(monitorsFrom(oneMonitor({workspace("1", json::array())})));
  EXPECT_TRUE(iconClasses(button).empty());
  EXPECT_FALSE(button->textLabel()->isHidden());
}

TEST_F(GlazewmWorkspacesTest, LeftPressOnIconButtonActivates) {
  EventBus bus;
  CommandRecorder recorder(bus);
  GlazewmWorkspacesConfig options;
  options.app_icons.enabled_populated = true;
  auto widget = make(bus, options);
  widget->show();
  widget->onWorkspaces(monitorsFrom(oneMonitor({workspace("web", {window(100)})})));
  GlazewmWorkspaceButtonBase* button = widget->buttonByName("web");
  ASSERT_NE(dynamic_cast<GlazewmIconButton*>(button), nullptr);
  recorder.take();
  QTest::mouseClick(button->widget(), Qt::RightButton);
  EXPECT_EQ(recorder.take(), json::array());
  QTest::mouseClick(button->widget(), Qt::LeftButton);
  EXPECT_EQ(recorder.take(), json::array({"command focus --workspace web"}));
}

TEST_F(GlazewmWorkspacesTest, WheelEventSwitchesWorkspaces) {
  const auto wheel = [](GlazewmWorkspacesWidget& widget, int delta) {
    QWheelEvent event(QPointF(1, 1), QPointF(1, 1), QPoint(0, 0), QPoint(0, delta), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
  };
  struct Case {
    bool exclusive;
    bool reverse;
    bool enabled;
    int delta;
    const char* expected;  // "" for none
  };
  const Case cases[] = {
      {true, false, true, -120, "command focus --next-active-workspace-on-monitor"},
      {true, false, true, 120, "command focus --prev-active-workspace-on-monitor"},
      {false, false, true, -120, "command focus --next-active-workspace"},
      {false, false, true, 120, "command focus --prev-active-workspace"},
      {true, true, true, -120, "command focus --prev-active-workspace-on-monitor"},
      {false, true, true, 120, "command focus --next-active-workspace"},
      {true, false, false, 120, ""},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(std::string(c.expected) + (c.enabled ? "" : " disabled"));
    EventBus bus;
    CommandRecorder recorder(bus);
    GlazewmWorkspacesConfig options;
    options.monitor_exclusive = c.exclusive;
    options.reverse_scroll_direction = c.reverse;
    options.enable_scroll_switching = c.enabled;
    auto widget = make(bus, options);
    widget->show();
    wheel(*widget, c.delta);
    json expected = json::array();
    if (*c.expected) expected.push_back(c.expected);
    EXPECT_EQ(recorder.take(), expected);
  }
}

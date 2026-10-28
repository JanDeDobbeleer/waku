#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/widgets/event_bus.h"
#include "core/widgets/registry.h"
#include "widgets/glazewm/glazewm_service.h"
#include "widgets/glazewm/glazewm_state.h"
#include "widgets/glazewm/glazewm_widgets.h"

using nlohmann::json;
using namespace yasb::widgets;
namespace gw = yasb::widgets::glazewm;

namespace {

json window(const std::string& id, const std::string& title, bool floating = false) {
  return {{"type", "window"},
          {"id", id},
          {"title", title},
          {"handle", 4242},
          {"className", "Chrome_WidgetWin_1"},
          {"processName", "chrome"},
          {"displayState", "shown"},
          {"state", {{"type", floating ? "floating" : "tiling"}}}};
}

json workspace(const std::string& name, bool focus, bool displayed, json children) {
  return {{"type", "workspace"},       {"name", name},
          {"displayName", name + "!"}, {"hasFocus", focus},
          {"isDisplayed", displayed},  {"children", std::move(children)}};
}

json sampleMonitors() {
  json split = {
      {"type", "split"},
      {"children",
       json::array({window("w2", "inner"),
                    json{{"type", "split"}, {"children", json::array({window("w3", "deep")})}}})}};
  json ws1 = workspace("1", true, true,
                       json::array({window("w1", "top"), split, window("w4", "float", true)}));
  json ws2 = workspace("2", false, false, json::array());
  json mon = {{"type", "monitor"},
              {"hardwareId", "DEL1234"},
              {"handle", 65537},
              {"children", json::array({ws1, ws2, json{{"type", "other"}}})}};
  return {{"monitors", json::array({mon})}};
}

}  // namespace

TEST(GlazewmState, ParsesMonitorsWorkspacesAndNestedWindows) {
  const auto monitors = gw::parseMonitors(sampleMonitors());
  ASSERT_TRUE(monitors);
  ASSERT_EQ(monitors->size(), 1u);
  const gw::Monitor& mon = monitors->front();
  EXPECT_EQ(mon.name, "DEL1234");
  EXPECT_EQ(mon.hwnd, 65537);
  ASSERT_EQ(mon.workspaces.size(), 2u);  // the non-workspace child is ignored
  const gw::Workspace& ws1 = mon.workspaces[0];
  EXPECT_EQ(ws1.name, "1");
  EXPECT_EQ(ws1.displayName, "1!");
  EXPECT_TRUE(ws1.focus);
  EXPECT_TRUE(ws1.isDisplayed);
  EXPECT_EQ(ws1.numWindows, 3);  // direct children, a split counts as one
  ASSERT_EQ(ws1.windows.size(), 4u);
  EXPECT_EQ(ws1.windows[0].title, "top");
  EXPECT_EQ(ws1.windows[1].title, "inner");
  EXPECT_EQ(ws1.windows[2].title, "deep");
  EXPECT_EQ(ws1.windows[3].title, "float");
  EXPECT_FALSE(ws1.windows[0].isFloating);
  EXPECT_TRUE(ws1.windows[3].isFloating);
  EXPECT_EQ(ws1.windows[0].id, "w1");
  EXPECT_EQ(ws1.windows[0].handle, 4242);
  EXPECT_EQ(ws1.windows[0].className, "Chrome_WidgetWin_1");
  EXPECT_EQ(ws1.windows[0].processName, "chrome");
  EXPECT_EQ(ws1.windows[0].displayState, "shown");
  const gw::Workspace& ws2 = mon.workspaces[1];
  EXPECT_FALSE(ws2.focus);
  EXPECT_FALSE(ws2.isDisplayed);
  EXPECT_EQ(ws2.numWindows, 0);
  EXPECT_TRUE(ws2.windows.empty());
}

TEST(GlazewmState, MonitorEdgeCases) {
  // Empty and missing list: no monitors, but not "ignore".
  auto empty = gw::parseMonitors(json{{"monitors", json::array()}});
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());
  auto missing = gw::parseMonitors(json::object());
  ASSERT_TRUE(missing);
  EXPECT_TRUE(missing->empty());
  // null (and any non-array) monitors: message ignored.
  EXPECT_FALSE(gw::parseMonitors(json{{"monitors", nullptr}}));
  EXPECT_FALSE(gw::parseMonitors(json{{"monitors", "x"}}));

  // Missing or zero handle skips the monitor; missing hardwareId falls back.
  json data = {{"monitors", json::array({json{{"hardwareId", "A"}, {"children", json::array()}},
                                         json{{"hardwareId", "B"}, {"handle", 0}},
                                         json{{"handle", 77}, {"children", nullptr}},
                                         json{{"hardwareId", ""}, {"handle", 78}}})}};
  auto monitors = gw::parseMonitors(data);
  ASSERT_TRUE(monitors);
  ASSERT_EQ(monitors->size(), 2u);
  EXPECT_EQ((*monitors)[0].name, "Unknown_77");
  EXPECT_TRUE((*monitors)[0].workspaces.empty());
  EXPECT_EQ((*monitors)[1].name, "Unknown_78");
}

TEST(GlazewmState, ToleratesMissingWindowFields) {
  json ws = {
      {"type", "workspace"},
      {"children", json::array({json{{"type", "window"}},
                                json{{"type", "window"}, {"title", nullptr}, {"state", 5}}})}};
  json data = {{"monitors", json::array({json{{"handle", 1}, {"children", json::array({ws})}}})}};
  auto monitors = gw::parseMonitors(data);
  ASSERT_TRUE(monitors);
  const gw::Workspace& w = monitors->front().workspaces.front();
  EXPECT_EQ(w.name, "");
  EXPECT_EQ(w.numWindows, 2);
  ASSERT_EQ(w.windows.size(), 2u);
  EXPECT_EQ(w.windows[1].title, "");
  EXPECT_FALSE(w.windows[1].isFloating);
}

TEST(GlazewmState, BindingModes) {
  auto none = gw::parseBindingModes(json{{"bindingModes", json::array()}});
  ASSERT_TRUE(none);
  EXPECT_FALSE(none->name);
  EXPECT_FALSE(none->displayName);

  auto some = gw::parseBindingModes(
      json{{"bindingModes", json::array({json{{"name", "resize"}, {"displayName", "Resize"}},
                                         json{{"name", "second"}}})}});
  ASSERT_TRUE(some);
  EXPECT_EQ(some->name, QStringLiteral("resize"));
  EXPECT_EQ(some->displayName, QStringLiteral("Resize"));

  auto partial = gw::parseBindingModes(json{{"bindingModes", json::array({json{{"name", "m"}}})}});
  ASSERT_TRUE(partial);
  EXPECT_EQ(partial->name, QStringLiteral("m"));
  EXPECT_FALSE(partial->displayName);

  EXPECT_FALSE(gw::parseBindingModes(json{{"bindingModes", nullptr}}));
}

TEST(GlazewmState, TilingDirection) {
  EXPECT_EQ(gw::parseTilingDirection(json{{"tilingDirection", "vertical"}}),
            gw::TilingDirection::Vertical);
  EXPECT_EQ(gw::parseTilingDirection(json{{"tilingDirection", "horizontal"}}),
            gw::TilingDirection::Horizontal);
  EXPECT_EQ(gw::parseTilingDirection(json::object()), gw::TilingDirection::Horizontal);
  EXPECT_EQ(gw::parseTilingDirection(json{{"tilingDirection", 3}}),
            gw::TilingDirection::Horizontal);
}

namespace {

constexpr const char* kWsSub =
    "sub -e workspace_activated workspace_deactivated workspace_updated focus_changed "
    "focused_container_moved";
constexpr const char* kTdSub =
    "sub -e focus_changed tiling_direction_changed focused_container_moved";

// Service wired to recorders instead of a socket.
struct Harness {
  EventBus bus;
  std::vector<std::string> sent;
  std::vector<QString> opened;
  std::vector<QVariantMap> events;  // kStateEvent payloads
  std::unique_ptr<gw::GlazewmService> service;

  Harness() {
    service = std::make_unique<gw::GlazewmService>(
        bus,
        [this](const QByteArray& bytes) {
          sent.push_back(bytes.toStdString());
          return true;
        },
        [this](const QString& uri) { opened.push_back(uri); });
    QObject::connect(&bus, &EventBus::event, service.get(),
                     [this](const QString& name, const QVariantMap& payload) {
                       if (name == gw::kStateEvent) events.push_back(payload);
                     });
  }

  void subscribe(const QString& topic, const QString& uri = QStringLiteral("ws://localhost:6123")) {
    gw::subscribe(bus, topic, uri);
  }
  void message(const json& value) { service->handleMessage(QString::fromStdString(value.dump())); }
  std::vector<QVariantMap> ofKind(const QString& kind) const {
    std::vector<QVariantMap> out;
    for (const QVariantMap& e : events) {
      if (e.value(QStringLiteral("kind")).toString() == kind) out.push_back(e);
    }
    return out;
  }
};

json response(const std::string& clientMessage, json data) {
  return {{"messageType", "client_response"},
          {"clientMessage", clientMessage},
          {"success", true},
          {"data", std::move(data)}};
}

}  // namespace

TEST(GlazewmService, RegisterIsCallable) {
  WidgetRegistry registry;
  registerGlazewmWidgets(registry);
}

TEST(GlazewmService, SendsSubscriptionUnionOnConnectAndOpensOnce) {
  Harness h;
  h.subscribe(gw::kTopicWorkspaces, QStringLiteral("ws://a:1"));
  h.subscribe(gw::kTopicTilingDirection, QStringLiteral("ws://b:2"));
  h.subscribe(gw::kTopicWorkspaces, QStringLiteral("ws://c:3"));
  ASSERT_EQ(h.opened.size(), 1u);
  EXPECT_EQ(h.opened[0], "ws://a:1");  // first uri wins
  EXPECT_TRUE(h.sent.empty());         // not connected yet

  h.service->handleConnected(true);
  EXPECT_TRUE(h.service->isConnected());
  const std::vector<std::string> expected = {kWsSub, "query monitors", kTdSub,
                                             "query tiling-direction"};
  EXPECT_EQ(h.sent, expected);
  ASSERT_EQ(h.ofKind("connection").size(), 1u);
  EXPECT_TRUE(h.ofKind("connection")[0].value("connected").toBool());

  // Reconnect resends everything.
  h.sent.clear();
  h.service->handleConnected(false);
  EXPECT_TRUE(h.sent.empty());
  EXPECT_FALSE(h.ofKind("connection").back().value("connected").toBool());
  h.service->handleConnected(true);
  EXPECT_EQ(h.sent, expected);
}

TEST(GlazewmService, TopicSubscribedWhileConnectedSendsAtOnce) {
  Harness h;
  h.subscribe(gw::kTopicBindingMode);
  h.service->handleConnected(true);
  EXPECT_EQ(h.sent,
            (std::vector<std::string>{"sub -e binding_modes_changed", "query binding-modes"}));
  h.sent.clear();
  h.subscribe(gw::kTopicWorkspaces);
  EXPECT_EQ(h.sent, (std::vector<std::string>{kWsSub, "query monitors"}));
  h.sent.clear();
  h.subscribe(gw::kTopicWorkspaces);  // second widget, same topic: nothing new to send
  h.subscribe(QStringLiteral("bogus"));
  EXPECT_TRUE(h.sent.empty());
  EXPECT_EQ(h.opened.size(), 1u);
}

TEST(GlazewmService, EventSubscriptionRequeriesSubscribedTopics) {
  Harness h;
  h.subscribe(gw::kTopicWorkspaces);
  h.subscribe(gw::kTopicBindingMode);
  h.service->handleConnected(true);
  h.sent.clear();
  h.message(json{{"messageType", "event_subscription"}, {"data", json::object()}});
  EXPECT_EQ(h.sent, (std::vector<std::string>{"query monitors", "query binding-modes"}));
}

TEST(GlazewmService, ClientResponseDispatch) {
  Harness h;
  h.subscribe(gw::kTopicWorkspaces);
  h.service->handleConnected(true);

  h.message(response("query monitors", sampleMonitors()));
  auto workspaces = h.ofKind("workspaces");
  ASSERT_EQ(workspaces.size(), 1u);
  auto monitors = gw::monitorsFromPayload(workspaces[0]);
  ASSERT_TRUE(monitors);
  ASSERT_EQ(monitors->size(), 1u);
  EXPECT_EQ(monitors->front().workspaces.size(), 2u);

  h.message(response("query tiling-direction", json{{"tilingDirection", "vertical"}}));
  auto dir = h.ofKind("tiling_direction");
  ASSERT_EQ(dir.size(), 1u);
  EXPECT_EQ(dir[0].value("direction").toInt(), static_cast<int>(gw::TilingDirection::Vertical));

  h.message(response("query binding-modes",
                     json{{"bindingModes", json::array({json{{"name", "resize"}}})}}));
  auto modes = h.ofKind("binding_mode");
  ASSERT_EQ(modes.size(), 1u);
  auto mode = gw::bindingModeFromPayload(modes[0]);
  ASSERT_TRUE(mode);
  EXPECT_EQ(mode->name, QStringLiteral("resize"));

  // Ignored: null monitors, null binding modes, non-object data, invalid JSON, unknown query.
  h.message(response("query monitors", json{{"monitors", nullptr}}));
  h.message(response("query binding-modes", json{{"bindingModes", nullptr}}));
  h.message(json{
      {"messageType", "client_response"}, {"clientMessage", "query monitors"}, {"data", nullptr}});
  h.message(response("something else", json::object()));
  h.service->handleMessage(QStringLiteral("not json"));
  h.message(json::array());
  EXPECT_EQ(h.ofKind("workspaces").size(), 1u);
  EXPECT_EQ(h.ofKind("binding_mode").size(), 1u);
  EXPECT_EQ(h.ofKind("tiling_direction").size(), 1u);
}

TEST(GlazewmService, LateSubscriberGetsReplay) {
  Harness h;
  h.subscribe(gw::kTopicWorkspaces);
  QCoreApplication::processEvents();
  h.service->handleConnected(true);
  h.message(response("query monitors", sampleMonitors()));
  h.message(response("query tiling-direction", json{{"tilingDirection", "vertical"}}));
  h.events.clear();

  h.subscribe(gw::kTopicWorkspaces);
  EXPECT_TRUE(h.events.empty());  // replay is queued
  QCoreApplication::processEvents();
  ASSERT_EQ(h.ofKind("connection").size(), 1u);
  EXPECT_TRUE(h.ofKind("connection")[0].value("connected").toBool());
  EXPECT_EQ(h.ofKind("workspaces").size(), 1u);
  EXPECT_TRUE(h.ofKind("tiling_direction").empty());  // other topic's data is not replayed

  h.events.clear();
  h.subscribe(gw::kTopicBindingMode);  // no binding-mode data yet: connection only
  QCoreApplication::processEvents();
  EXPECT_EQ(h.ofKind("connection").size(), 1u);
  EXPECT_TRUE(h.ofKind("binding_mode").empty());
}

TEST(GlazewmService, CommandPassthroughAndHelpers) {
  Harness h;
  gw::activateWorkspace(h.bus, QStringLiteral("2"));
  gw::toggleTilingDirection(h.bus);
  gw::disableBindingMode(h.bus, QStringLiteral("resize"));
  gw::enableBindingMode(h.bus, QStringLiteral("resize"));
  gw::focusNextWorkspace(h.bus);
  gw::focusPrevWorkspace(h.bus);
  gw::focusNextWorkspaceGlobal(h.bus);
  gw::focusPrevWorkspaceGlobal(h.bus);
  gw::sendCommand(h.bus, QStringLiteral("focus --workspace 9"));  // not a full command: dropped
  const std::vector<std::string> expected = {"command focus --workspace 2",
                                             "command toggle-tiling-direction",
                                             "command wm-disable-binding-mode --name resize",
                                             "command wm-enable-binding-mode --name resize",
                                             "command focus --next-active-workspace-on-monitor",
                                             "command focus --prev-active-workspace-on-monitor",
                                             "command focus --next-active-workspace",
                                             "command focus --prev-active-workspace"};
  EXPECT_EQ(h.sent, expected);
}

TEST(GlazewmService, StopIsIdempotentAndSilencesService) {
  Harness h;
  h.subscribe(gw::kTopicWorkspaces);
  h.service->handleConnected(true);
  h.sent.clear();
  h.service->stop();
  h.service->stop();
  EXPECT_FALSE(h.service->isConnected());
  gw::toggleTilingDirection(h.bus);
  h.subscribe(gw::kTopicTilingDirection);
  h.service->handleConnected(true);
  EXPECT_TRUE(h.sent.empty());
}

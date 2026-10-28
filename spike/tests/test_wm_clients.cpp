#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "spike/services/glazewm_client.h"
#include "spike/services/komorebi_client.h"

using nlohmann::json;
using namespace barnacle::services;
namespace gw = yasb::widgets::glazewm;
namespace kmb = yasb::widgets::komorebi;
namespace platform = yasb::platform;

namespace {

// ---- GlazeWM ----

json window(const std::string& id) {
  return {{"type", "window"},
          {"id", id},
          {"title", "t"},
          {"handle", 4242},
          {"className", "c"},
          {"processName", "p"},
          {"displayState", "shown"},
          {"state", {{"type", "tiling"}}}};
}

json workspace(const std::string& name, bool focus, bool displayed, json children) {
  return {{"type", "workspace"},       {"name", name},
          {"displayName", name + "!"}, {"hasFocus", focus},
          {"isDisplayed", displayed},  {"children", std::move(children)}};
}

json monitorsData() {
  json mon = {{"type", "monitor"},
              {"hardwareId", "DEL1234"},
              {"handle", 65537},
              {"children", json::array({workspace("1", true, true, json::array({window("a")})),
                                        workspace("2", false, false, json::array())})}};
  return {{"monitors", json::array({mon})}};
}

json response(const std::string& clientMessage, json data) {
  return {{"messageType", "client_response"},
          {"clientMessage", clientMessage},
          {"success", true},
          {"data", std::move(data)}};
}

struct GlazeHarness {
  std::vector<std::string> sent;
  std::vector<QString> opened;
  std::unique_ptr<GlazeWM> glaze;

  GlazeHarness() {
    glaze = std::make_unique<GlazeWM>(
        [this](const QByteArray& bytes) {
          sent.push_back(bytes.toStdString());
          return true;
        },
        [this](const QString& uri) { opened.push_back(uri); }, nullptr);
  }

  void message(const json& value) {
    glaze->service().handleMessage(QString::fromStdString(value.dump()));
  }
};

// ---- komorebi ----

json komorebiWorkspace(const std::string& name, int windows) {
  json containers = json::array();
  for (int i = 0; i < windows; ++i) {
    containers.push_back({{"windows", {{"elements", json::array({json{{"hwnd", 100 + i}}})}}}});
  }
  return {{"name", name},
          {"containers", {{"elements", containers}}},
          {"monocle_container", nullptr},
          {"maximized_window", nullptr},
          {"floating_windows", {{"elements", json::array()}}}};
}

kmb::JsonPtr komorebiState() {
  json screen = {{"id", 65537},
                 {"name", "DISPLAY1"},
                 {"workspaces",
                  {{"elements", json::array({komorebiWorkspace("I", 0), komorebiWorkspace("II", 2),
                                             komorebiWorkspace("", 1)})},
                   {"focused", 1}}}};
  json second = {
      {"id", 131073},
      {"name", "DISPLAY2"},
      {"workspaces", {{"elements", json::array({komorebiWorkspace("A", 0)})}, {"focused", 0}}}};
  return std::make_shared<const json>(
      json{{"monitors", {{"elements", json::array({screen, second})}, {"focused", 0}}}});
}

struct KomorebiHarness {
  std::vector<std::vector<QStringList>> runs;
  std::unique_ptr<Komorebi> komorebi;

  KomorebiHarness() {
    platform::KomorebiBackend backend;
    backend.isKomorebiRunning = [] { return false; };  // the listener thread stays idle
    backend.runKomorebic = [](const QStringList&, int) { return platform::KomorebicResult{}; };
    komorebi = std::make_unique<Komorebi>(
        backend, [this](const std::vector<QStringList>& commands) { runs.push_back(commands); },
        QStringLiteral("yasb-native-services-test"), platform::KomorebiPipeListener::Timing{},
        nullptr);
  }

  void publish(const QString& kind, const kmb::JsonPtr& state = nullptr) {
    QVariantMap payload{{QStringLiteral("kind"), kind}};
    if (state) payload.insert(QStringLiteral("state"), QVariant::fromValue(state));
    emit komorebi->bus().event(kmb::kStateEvent, payload);
  }
};

}  // namespace

// ---- GlazeWM ----

TEST(GlazeWMClient, SubscribesToEveryTopicAndOpensTheDefaultSocketOnce) {
  GlazeHarness h;
  ASSERT_EQ(h.opened.size(), 1u);
  EXPECT_EQ(h.opened[0], QStringLiteral("ws://localhost:6123"));
  EXPECT_FALSE(h.glaze->connected());
  EXPECT_TRUE(h.glaze->monitors().isEmpty());
  EXPECT_EQ(h.glaze->tilingDirection(), QStringLiteral("horizontal"));
  EXPECT_TRUE(h.glaze->bindingMode().isEmpty());
}

TEST(GlazeWMClient, ConnectingSendsTheSubscriptionsAndQueries) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  EXPECT_TRUE(h.glaze->connected());
  const auto sentContains = [&](const std::string& text) {
    return std::any_of(h.sent.begin(), h.sent.end(),
                       [&](const std::string& s) { return s.find(text) != std::string::npos; });
  };
  EXPECT_TRUE(sentContains("query monitors"));
  EXPECT_TRUE(sentContains("query tiling-direction"));
  EXPECT_TRUE(sentContains("query binding-modes"));
}

TEST(GlazeWMClient, DisconnectIsReported) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  QSignalSpy changed(h.glaze.get(), &GlazeWM::changed);
  h.glaze->service().handleConnected(false);
  EXPECT_FALSE(h.glaze->connected());
  EXPECT_GE(changed.count(), 1);
}

TEST(GlazeWMClient, MapsMonitorsAndWorkspacesForQml) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  QSignalSpy changed(h.glaze.get(), &GlazeWM::changed);
  h.message(response("query monitors", monitorsData()));
  EXPECT_GE(changed.count(), 1);

  const QVariantList monitors = h.glaze->monitors();
  ASSERT_EQ(monitors.size(), 1);
  const QVariantMap monitor = monitors[0].toMap();
  EXPECT_EQ(monitor.value(QStringLiteral("hwnd")).toLongLong(), 65537);
  EXPECT_EQ(monitor.value(QStringLiteral("name")).toString(), QStringLiteral("DEL1234"));
  const QVariantList workspaces = monitor.value(QStringLiteral("workspaces")).toList();
  ASSERT_EQ(workspaces.size(), 2);
  const QVariantMap first = workspaces[0].toMap();
  EXPECT_EQ(first.value(QStringLiteral("name")).toString(), QStringLiteral("1"));
  EXPECT_EQ(first.value(QStringLiteral("displayName")).toString(), QStringLiteral("1!"));
  EXPECT_TRUE(first.value(QStringLiteral("focused")).toBool());
  EXPECT_TRUE(first.value(QStringLiteral("displayed")).toBool());
  EXPECT_EQ(first.value(QStringLiteral("windowCount")).toInt(), 1);
  EXPECT_TRUE(first.value(QStringLiteral("hasWindows")).toBool());
  const QVariantMap second = workspaces[1].toMap();
  EXPECT_FALSE(second.value(QStringLiteral("focused")).toBool());
  EXPECT_FALSE(second.value(QStringLiteral("hasWindows")).toBool());
}

TEST(GlazeWMClient, TracksTilingDirectionAndBindingMode) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  h.message(response("query tiling-direction", json{{"tilingDirection", "vertical"}}));
  EXPECT_EQ(h.glaze->tilingDirection(), QStringLiteral("vertical"));
  h.message(response("query tiling-direction", json{{"tilingDirection", "horizontal"}}));
  EXPECT_EQ(h.glaze->tilingDirection(), QStringLiteral("horizontal"));

  h.message(response("query binding-modes",
                     json{{"bindingModes", json::array({json{{"name", "resize"}}})}}));
  EXPECT_EQ(h.glaze->bindingMode(), QStringLiteral("resize"));
  h.message(response("query binding-modes", json{{"bindingModes", json::array()}}));
  EXPECT_TRUE(h.glaze->bindingMode().isEmpty());
}

TEST(GlazeWMClient, IgnoresGarbage) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  h.glaze->service().handleMessage(QStringLiteral("not json"));
  h.message(json::array());
  h.message(response("query monitors", json{{"monitors", nullptr}}));
  EXPECT_TRUE(h.glaze->monitors().isEmpty());
}

TEST(GlazeWMClient, CommandsAreSentAsTheServiceFormatsThem) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  h.sent.clear();

  h.glaze->activateWorkspace(QStringLiteral("2"));
  h.glaze->focusNextWorkspace();
  h.glaze->focusPreviousWorkspace();
  h.glaze->toggleTilingDirection();
  h.glaze->enableBindingMode(QStringLiteral("resize"));
  h.glaze->disableBindingMode(QStringLiteral("resize"));

  ASSERT_EQ(h.sent.size(), 6u);
  EXPECT_EQ(h.sent[0], gw::activateWorkspaceCommand(QStringLiteral("2")).toStdString());
  EXPECT_EQ(h.sent[1], gw::focusNextWorkspaceCommand().toStdString());
  EXPECT_EQ(h.sent[2], gw::focusPrevWorkspaceCommand().toStdString());
  EXPECT_EQ(h.sent[3], gw::toggleTilingDirectionCommand().toStdString());
  EXPECT_EQ(h.sent[4], gw::enableBindingModeCommand(QStringLiteral("resize")).toStdString());
  EXPECT_EQ(h.sent[5], gw::disableBindingModeCommand(QStringLiteral("resize")).toStdString());
}

// ---- komorebi ----

TEST(KomorebiClient, StartsDisconnectedAndEmpty) {
  KomorebiHarness h;
  EXPECT_FALSE(h.komorebi->connected());
  EXPECT_TRUE(h.komorebi->monitors().isEmpty());
}

TEST(KomorebiClient, MapsMonitorsAndWorkspacesForQml) {
  KomorebiHarness h;
  QSignalSpy changed(h.komorebi.get(), &Komorebi::changed);
  h.publish(QStringLiteral("connect"), komorebiState());
  EXPECT_TRUE(h.komorebi->connected());
  EXPECT_EQ(changed.count(), 1);

  const QVariantList monitors = h.komorebi->monitors();
  ASSERT_EQ(monitors.size(), 2);
  const QVariantMap first = monitors[0].toMap();
  EXPECT_EQ(first.value(QStringLiteral("index")).toInt(), 0);
  EXPECT_EQ(first.value(QStringLiteral("id")).toLongLong(), 65537);
  EXPECT_EQ(first.value(QStringLiteral("name")).toString(), QStringLiteral("DISPLAY1"));
  EXPECT_EQ(monitors[1].toMap().value(QStringLiteral("index")).toInt(), 1);
  EXPECT_EQ(monitors[1].toMap().value(QStringLiteral("id")).toLongLong(), 131073);

  const QVariantList workspaces = first.value(QStringLiteral("workspaces")).toList();
  ASSERT_EQ(workspaces.size(), 3);
  const QVariantMap empty = workspaces[0].toMap();
  EXPECT_EQ(empty.value(QStringLiteral("name")).toString(), QStringLiteral("I"));
  EXPECT_FALSE(empty.value(QStringLiteral("focused")).toBool());
  EXPECT_FALSE(empty.value(QStringLiteral("hasWindows")).toBool());
  EXPECT_EQ(empty.value(QStringLiteral("windowCount")).toInt(), 0);
  const QVariantMap focused = workspaces[1].toMap();
  EXPECT_TRUE(focused.value(QStringLiteral("focused")).toBool());
  EXPECT_TRUE(focused.value(QStringLiteral("hasWindows")).toBool());
  EXPECT_EQ(focused.value(QStringLiteral("windowCount")).toInt(), 2);
  const QVariantMap unnamed = workspaces[2].toMap();
  EXPECT_TRUE(unnamed.value(QStringLiteral("name")).toString().isEmpty());
  EXPECT_EQ(unnamed.value(QStringLiteral("index")).toInt(), 2);
}

TEST(KomorebiClient, UpdateReplacesTheState) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), komorebiState());
  const auto smaller = std::make_shared<const json>(
      json{{"monitors", {{"elements", json::array()}, {"focused", 0}}}});
  h.publish(QStringLiteral("update"), smaller);
  EXPECT_TRUE(h.komorebi->connected());
  EXPECT_TRUE(h.komorebi->monitors().isEmpty());
}

TEST(KomorebiClient, DisconnectClearsMonitors) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), komorebiState());
  QSignalSpy changed(h.komorebi.get(), &Komorebi::changed);
  h.publish(QStringLiteral("disconnect"));
  EXPECT_FALSE(h.komorebi->connected());
  EXPECT_TRUE(h.komorebi->monitors().isEmpty());
  EXPECT_EQ(changed.count(), 1);
}

TEST(KomorebiClient, IgnoresUnknownEventsAndMissingState) {
  KomorebiHarness h;
  QSignalSpy changed(h.komorebi.get(), &Komorebi::changed);
  h.publish(QStringLiteral("something-else"));
  h.publish(QStringLiteral("connect"));  // no state payload
  EXPECT_EQ(changed.count(), 0);
  EXPECT_FALSE(h.komorebi->connected());
}

TEST(KomorebiClient, FocusWorkspaceRunsKomorebic) {
  KomorebiHarness h;
  h.komorebi->focusWorkspace(1, 3);
  ASSERT_EQ(h.runs.size(), 1u);
  ASSERT_EQ(h.runs[0].size(), 1u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("focus-monitor-workspace 1 3"));
}

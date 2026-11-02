#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "features/workspaces/testing/wm_fixtures.h"

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

// A state event that leaves what the bar shows as it was (komorebi sends one for every window
// move and title change) must not emit: the bindings and the workspace delegates would re-run.
TEST(KomorebiClient, AnEventThatChangesNothingTheBarShowsDoesNotEmit) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), komorebiState());
  QSignalSpy changed(h.komorebi.get(), &Komorebi::changed);

  h.publish(QStringLiteral("update"), komorebiState());
  json noisy = *komorebiState();
  noisy["unrelated"] = "a window title changed";
  h.publish(QStringLiteral("update"), std::make_shared<const json>(noisy));
  EXPECT_EQ(changed.count(), 0);

  h.publish(QStringLiteral("update"), komorebiState(0));  // another workspace is shown
  EXPECT_EQ(changed.count(), 1);
}

TEST(KomorebiClient, ASecondDisconnectDoesNotEmit) {
  KomorebiHarness h;
  QSignalSpy changed(h.komorebi.get(), &Komorebi::changed);
  h.publish(QStringLiteral("disconnect"));  // never connected
  EXPECT_EQ(changed.count(), 0);
  h.publish(QStringLiteral("connect"), komorebiState());
  h.publish(QStringLiteral("disconnect"));
  h.publish(QStringLiteral("disconnect"));
  EXPECT_EQ(changed.count(), 2);
}

TEST(GlazeWMClient, RepeatingTheCurrentStateDoesNotEmit) {
  GlazeHarness h;
  h.glaze->service().handleConnected(true);
  h.message(response("query monitors", monitorsData()));
  h.message(response("query tiling-direction", json{{"tilingDirection", "vertical"}}));
  QSignalSpy changed(h.glaze.get(), &GlazeWM::changed);
  h.glaze->service().handleConnected(true);
  h.message(response("query monitors", monitorsData()));
  h.message(response("query tiling-direction", json{{"tilingDirection", "vertical"}}));
  EXPECT_EQ(changed.count(), 0);
  h.message(response("query tiling-direction", json{{"tilingDirection", "horizontal"}}));
  EXPECT_EQ(changed.count(), 1);
}

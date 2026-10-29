#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>

#include "spike/services/workspaces_service.h"
#include "spike/tests/wm_fixtures.h"

namespace {

QVariantMap map(const QVariant& v) { return v.toMap(); }
QVariantList list(const QVariant& v) { return v.toList(); }

QVariantMap monitorAt(const Workspaces& w, int i) { return map(w.monitors().at(i)); }
QVariantMap workspaceAt(const Workspaces& w, int monitor, int i) {
  return map(list(monitorAt(w, monitor).value(QStringLiteral("workspaces"))).at(i));
}

// Both backends on their test seams, plus the model under test.
struct Harness {
  std::vector<std::vector<QStringList>> runs;
  std::vector<std::string> sent;
  std::unique_ptr<Komorebi> komorebi;
  std::unique_ptr<GlazeWM> glaze;
  std::unique_ptr<Workspaces> workspaces;

  Harness() {
    platform::KomorebiBackend backend;
    backend.isKomorebiRunning = [] { return false; };
    backend.runKomorebic = [](const QStringList&, int) { return platform::KomorebicResult{}; };
    komorebi = std::make_unique<Komorebi>(
        backend, [this](const std::vector<QStringList>& commands) { runs.push_back(commands); },
        QStringLiteral("wabar-workspaces-test"), platform::KomorebiPipeListener::Timing{}, nullptr);
    glaze = std::make_unique<GlazeWM>(
        [this](const QByteArray& bytes) {
          sent.push_back(bytes.toStdString());
          return true;
        },
        [](const QString&) {}, nullptr);
    workspaces = std::make_unique<Workspaces>(komorebi.get(), glaze.get());
  }

  void komorebiConnect(int focusedWorkspace = 1, int focusedMonitor = 0) {
    QVariantMap payload{{QStringLiteral("kind"), QStringLiteral("connect")},
                        {QStringLiteral("state"),
                         QVariant::fromValue(komorebiState(focusedWorkspace, focusedMonitor))}};
    emit komorebi->bus().event(kmb::kStateEvent, payload);
  }
  void komorebiDisconnect() {
    emit komorebi->bus().event(kmb::kStateEvent,
                               {{QStringLiteral("kind"), QStringLiteral("disconnect")}});
  }
  void glazeConnect() {
    glaze->service().handleConnected(true);
    glaze->service().handleMessage(
        QString::fromStdString(response("query monitors", monitorsData()).dump()));
    sent.clear();
  }
};

}  // namespace

TEST(WorkspacesModel, HasNoProviderWhenNothingIsConnected) {
  Harness h;
  EXPECT_EQ(h.workspaces->provider(), QString());
  EXPECT_FALSE(h.workspaces->connected());
  EXPECT_TRUE(h.workspaces->monitors().isEmpty());
}

TEST(WorkspacesModel, MapsKomorebi) {
  Harness h;
  QSignalSpy changed(h.workspaces.get(), &Workspaces::changed);
  h.komorebiConnect(1, 0);
  EXPECT_GE(changed.count(), 1);
  EXPECT_EQ(h.workspaces->provider(), QStringLiteral("komorebi"));
  EXPECT_TRUE(h.workspaces->connected());
  ASSERT_EQ(h.workspaces->monitors().size(), 2);

  const QVariantMap first = monitorAt(*h.workspaces, 0);
  EXPECT_EQ(first.value(QStringLiteral("handle")).toLongLong(), 65537);
  EXPECT_EQ(first.value(QStringLiteral("name")).toString(), QStringLiteral("DISPLAY1"));
  ASSERT_EQ(list(first.value(QStringLiteral("workspaces"))).size(), 3);

  const QVariantMap w0 = workspaceAt(*h.workspaces, 0, 0);
  EXPECT_EQ(w0.value(QStringLiteral("id")).toString(), QStringLiteral("0"));
  EXPECT_EQ(w0.value(QStringLiteral("label")).toString(), QStringLiteral("I"));
  EXPECT_FALSE(w0.value(QStringLiteral("active")).toBool());
  EXPECT_FALSE(w0.value(QStringLiteral("hasWindows")).toBool());

  const QVariantMap w1 = workspaceAt(*h.workspaces, 0, 1);
  EXPECT_TRUE(w1.value(QStringLiteral("active")).toBool());
  EXPECT_TRUE(w1.value(QStringLiteral("focused")).toBool());  // active on the focused monitor
  EXPECT_EQ(w1.value(QStringLiteral("windowCount")).toInt(), 2);

  // An unnamed workspace is labelled with its 1-based index.
  EXPECT_EQ(workspaceAt(*h.workspaces, 0, 2).value(QStringLiteral("label")).toString(),
            QStringLiteral("3"));

  // The second monitor shows workspace A, but it is not the focused monitor.
  const QVariantMap a = workspaceAt(*h.workspaces, 1, 0);
  EXPECT_TRUE(a.value(QStringLiteral("active")).toBool());
  EXPECT_FALSE(a.value(QStringLiteral("focused")).toBool());
}

TEST(WorkspacesModel, FocusFollowsTheFocusedMonitor) {
  Harness h;
  h.komorebiConnect(1, 1);
  EXPECT_FALSE(workspaceAt(*h.workspaces, 0, 1).value(QStringLiteral("focused")).toBool());
  EXPECT_TRUE(workspaceAt(*h.workspaces, 1, 0).value(QStringLiteral("focused")).toBool());
}

TEST(WorkspacesModel, MapsGlazeWM) {
  Harness h;
  h.glazeConnect();
  EXPECT_EQ(h.workspaces->provider(), QStringLiteral("glazewm"));
  ASSERT_EQ(h.workspaces->monitors().size(), 1);
  EXPECT_EQ(monitorAt(*h.workspaces, 0).value(QStringLiteral("handle")).toLongLong(), 65537);

  const QVariantMap first = workspaceAt(*h.workspaces, 0, 0);
  EXPECT_EQ(first.value(QStringLiteral("id")).toString(), QStringLiteral("1"));
  EXPECT_EQ(first.value(QStringLiteral("label")).toString(), QStringLiteral("1!"));
  EXPECT_TRUE(first.value(QStringLiteral("active")).toBool());
  EXPECT_TRUE(first.value(QStringLiteral("focused")).toBool());
  EXPECT_TRUE(first.value(QStringLiteral("hasWindows")).toBool());
  EXPECT_EQ(first.value(QStringLiteral("windowCount")).toInt(), 1);

  const QVariantMap second = workspaceAt(*h.workspaces, 0, 1);
  EXPECT_FALSE(second.value(QStringLiteral("active")).toBool());
  EXPECT_FALSE(second.value(QStringLiteral("hasWindows")).toBool());
}

TEST(WorkspacesModel, PreferChoosesBetweenTwoConnectedManagers) {
  Harness h;
  h.komorebiConnect();
  h.glazeConnect();
  EXPECT_EQ(h.workspaces->provider(), QStringLiteral("komorebi"));  // the default
  h.workspaces->setPrefer(QStringLiteral("glazewm"));
  EXPECT_EQ(h.workspaces->provider(), QStringLiteral("glazewm"));
  h.workspaces->setPrefer(QStringLiteral("junk"));
  EXPECT_EQ(h.workspaces->provider(), QStringLiteral("komorebi"));
}

TEST(WorkspacesModel, FallsBackWhenThePreferredManagerGoesAway) {
  Harness h;
  h.komorebiConnect();
  h.glazeConnect();
  h.komorebiDisconnect();
  EXPECT_EQ(h.workspaces->provider(), QStringLiteral("glazewm"));
  EXPECT_EQ(h.workspaces->monitors().size(), 1);
  h.glaze->service().handleConnected(false);
  EXPECT_FALSE(h.workspaces->connected());
  EXPECT_TRUE(h.workspaces->monitors().isEmpty());
}

TEST(WorkspacesModel, FocusRunsKomorebicWithTheMonitorIndex) {
  Harness h;
  h.komorebiConnect();
  h.workspaces->focus(65537, QStringLiteral("2"));
  h.workspaces->focus(131073, QStringLiteral("0"));  // the second monitor is index 1
  ASSERT_EQ(h.runs.size(), 2u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("focus-monitor-workspace 0 2"));
  EXPECT_EQ(h.runs[1][0].join(' '), QStringLiteral("focus-monitor-workspace 1 0"));
}

TEST(WorkspacesModel, FocusIgnoresUnknownMonitorsAndIds) {
  Harness h;
  h.komorebiConnect();
  h.workspaces->focus(999, QStringLiteral("1"));
  h.workspaces->focus(65537, QStringLiteral("not-a-number"));
  EXPECT_TRUE(h.runs.empty());

  Harness g;
  g.glazeConnect();
  g.workspaces->focus(999, QStringLiteral("2"));
  EXPECT_TRUE(g.sent.empty());
}

TEST(WorkspacesModel, FocusSendsTheGlazeWMCommand) {
  Harness h;
  h.glazeConnect();
  h.workspaces->focus(65537, QStringLiteral("2"));
  ASSERT_EQ(h.sent.size(), 1u);
  EXPECT_EQ(h.sent[0], gw::activateWorkspaceCommand(QStringLiteral("2")).toStdString());
}

TEST(WorkspacesModel, NextAndPreviousWrapAroundTheMonitor) {
  Harness h;
  h.komorebiConnect(1);  // active workspace 1 of 3
  h.workspaces->focusNext(65537);
  h.workspaces->focusPrevious(65537);
  ASSERT_EQ(h.runs.size(), 2u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("focus-monitor-workspace 0 2"));
  EXPECT_EQ(h.runs[1][0].join(' '), QStringLiteral("focus-monitor-workspace 0 0"));

  h.runs.clear();
  h.komorebiConnect(2);  // last workspace: next wraps to the first
  h.workspaces->focusNext(65537);
  h.komorebiConnect(0);  // first workspace: previous wraps to the last
  h.workspaces->focusPrevious(65537);
  ASSERT_EQ(h.runs.size(), 2u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("focus-monitor-workspace 0 0"));
  EXPECT_EQ(h.runs[1][0].join(' '), QStringLiteral("focus-monitor-workspace 0 2"));
}

TEST(WorkspacesModel, NextWorksForGlazeWMAndIgnoresUnknownMonitors) {
  Harness h;
  h.glazeConnect();  // workspaces "1" (active) and "2"
  h.workspaces->focusNext(65537);
  ASSERT_EQ(h.sent.size(), 1u);
  EXPECT_EQ(h.sent[0], gw::activateWorkspaceCommand(QStringLiteral("2")).toStdString());
  h.workspaces->focusNext(999);
  EXPECT_EQ(h.sent.size(), 1u);
}

TEST(WorkspacesModel, ChangedIsEmittedWhenABackendChanges) {
  Harness h;
  QSignalSpy changed(h.workspaces.get(), &Workspaces::changed);
  h.komorebiConnect();
  const int afterKomorebi = changed.count();
  EXPECT_GE(afterKomorebi, 1);
  h.glazeConnect();
  EXPECT_GT(changed.count(), afterKomorebi);
}

TEST(WorkspacesModel, WorksWithoutBackends) {
  Workspaces none(nullptr, nullptr);
  EXPECT_FALSE(none.connected());
  none.focus(1, QStringLiteral("1"));  // no crash
  none.focusNext(1);
  SUCCEED();
}

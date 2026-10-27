#include <gtest/gtest.h>

#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <memory>

#include "core/bar/theme_class.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/callbacks.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"

using namespace yasb::widgets;

namespace {

class CallbacksTest : public testing::Test {
 protected:
  void SetUp() override {
    savedLauncher_ = processLauncher();
    savedFunctions_ = systemFunctions();
    processLauncher() = [this](const QString& cmd) {
      launched_ << cmd;
      return true;
    };
    systemFunctions().clear();
  }
  void TearDown() override {
    processLauncher() = savedLauncher_;
    systemFunctions() = savedFunctions_;
  }

  QStringList launched_;
  ProcessLauncher savedLauncher_;
  SystemFunctionMap savedFunctions_;
};

}  // namespace

TEST(CallbackParse, SplitsOnlyWhenThereIsASpace) {
  auto p = parseCallback("toggle_label");
  EXPECT_EQ(p.name, "toggle_label");
  EXPECT_TRUE(p.args.isEmpty());

  p = parseCallback("exec \"C:\\Program Files\\app.exe\" --flag  x");
  EXPECT_EQ(p.name, "exec");
  ASSERT_EQ(p.args.size(), 3);
  EXPECT_EQ(p.args[0], "C:\\Program Files\\app.exe");
  EXPECT_EQ(p.args[1], "--flag");
  EXPECT_EQ(p.args[2], "x");
}

TEST(CallbackParse, QuoteCmdLineFollowsList2CmdLine) {
  EXPECT_EQ(quoteCmdLine({"notepad", "a b", ""}), "notepad \"a b\" \"\"");
  EXPECT_EQ(quoteCmdLine({"say \"hi\""}), "\"say \\\"hi\\\"\"");
  EXPECT_EQ(quoteCmdLine({"C:\\dir with space\\"}), "\"C:\\dir with space\\\\\"");
}

TEST_F(CallbacksTest, RegistryDispatchesWithArityChecks) {
  CallbackRegistry reg;
  QStringList got;
  reg.add("one", [&](const QStringList& a) { got = a; }, 1);
  reg.add("boom", [](const QStringList&) { throw std::runtime_error("x"); }, 0);

  EXPECT_TRUE(reg.run("one \"a b\""));
  EXPECT_EQ(got, QStringList{"a b"});
  EXPECT_FALSE(reg.run("one"));
  EXPECT_FALSE(reg.run("one a b"));
  EXPECT_FALSE(reg.run("missing"));
  EXPECT_FALSE(reg.run("boom"));
  EXPECT_TRUE(reg.run("do_nothing"));
  EXPECT_TRUE(reg.run("default"));
}

TEST_F(CallbacksTest, ExecPrefersSystemFunctionsThenLaunchesProcess) {
  CallbackRegistry reg;
  bool called = false;
  systemFunctions()["search"] = [&] { called = true; };

  EXPECT_TRUE(reg.run("exec search"));
  EXPECT_TRUE(called);
  EXPECT_TRUE(launched_.isEmpty());

  EXPECT_TRUE(reg.run("exec notepad \"a b\""));
  ASSERT_EQ(launched_.size(), 1);
  EXPECT_EQ(launched_[0], "notepad \"a b\"");
}

TEST_F(CallbacksTest, MouseButtonsRouteToBoundCallbacks) {
  EventBus bus;
  QWidget host;
  auto* w = new BaseWidget("cpu", bus, "cpu-widget", 0, &host);
  QString last;
  w->registerCallback("l", [&](const QStringList&) { last = "left"; });
  w->registerCallback("m", [&](const QStringList&) { last = "middle"; });
  w->registerCallback("r", [&](const QStringList&) { last = "right"; });
  CallbackBinding b;
  b.left = "l";
  b.middle = "m";
  b.right = "r";
  w->setCallbackBinding(b);

  QTest::mouseClick(w, Qt::LeftButton);
  EXPECT_EQ(last, "left");
  QTest::mouseClick(w, Qt::MiddleButton);
  EXPECT_EQ(last, "middle");
  QTest::mouseClick(w, Qt::RightButton);
  EXPECT_EQ(last, "right");
}

TEST_F(CallbacksTest, HotkeysRouteByWidgetNameAndScreen) {
  EventBus bus;
  QWidget host;
  auto* w = new BaseWidget("cpu", bus, "", 0, &host);
  int hits = 0;
  w->registerCallback("hit", [&](const QStringList&) { ++hits; });
  w->setScreenName("DISPLAY1");

  emit bus.widgetHotkey("cpu", "hit", "DISPLAY1");
  EXPECT_EQ(hits, 1);
  emit bus.widgetHotkey("memory", "hit", "DISPLAY1");
  emit bus.widgetHotkey("cpu", "hit", "DISPLAY2");
  emit bus.widgetHotkey("cpu", "hit", "");
  EXPECT_EQ(hits, 1);
  w->setHotkeyEnabled(false);
  emit bus.widgetHotkey("cpu", "hit", "DISPLAY1");
  EXPECT_EQ(hits, 1);
}

TEST_F(CallbacksTest, TimerCallbackRunsImmediatelyOnStart) {
  EventBus bus;
  QWidget host;
  auto* w = new BaseWidget("cpu", bus, "", 0, &host);
  int ticks = 0;
  w->registerCallback("update", [&](const QStringList&) { ++ticks; });
  CallbackBinding b;
  b.timer = "update";
  w->setCallbackBinding(b);
  w->startTimer();
  EXPECT_EQ(ticks, 1);
}

TEST_F(CallbacksTest, RegistryBuildsKnownTypesOnly) {
  EventBus bus;
  WidgetRegistry reg;
  reg.add("test.Widget", [](const WidgetContext& ctx) {
    return new BaseWidget(QString::fromStdString(ctx.name), ctx.eventBus);
  });
  yasb::bar::GlobalState globalState;
  PollingWorkerRegistry pollingWorkers;
  WidgetContext ctx{"w1", "test.Widget", {}, "DISPLAY1", "bar-a", bus, globalState, pollingWorkers};
  std::unique_ptr<BaseWidget> w(reg.create(ctx));
  ASSERT_TRUE(w);
  EXPECT_EQ(w->screenName(), "DISPLAY1");
  EXPECT_EQ(w->barId(), "bar-a");
  ctx.type = "nope.Widget";
  EXPECT_EQ(reg.create(ctx), nullptr);
}

TEST_F(CallbacksTest, PollingWorkerDeliversOnOwnerThread) {
  std::atomic<int> n{0};
  PollingWorker worker([&] { return QVariant(++n); }, 20);
  QSignalSpy spy(&worker, &PollingWorker::dataReady);
  worker.start();
  ASSERT_TRUE(spy.wait(2000));
  EXPECT_EQ(spy.first().first().toInt(), 1);
  for (int i = 0; i < 100 && spy.count() < 3; ++i) QTest::qWait(20);
  EXPECT_GE(spy.count(), 3);
  worker.stop();
  EXPECT_FALSE(worker.isRunning());
}

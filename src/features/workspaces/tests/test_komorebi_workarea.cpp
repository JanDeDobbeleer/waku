// KomorebiWorkAreaGuard on a fake notifier and a fake clock: when it notifies komorebi after the
// screens change. No test reaches the real komorebi: the notifier is a fake, and the real one
// exists only on the live desktop.
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QStringList>
#include <QTest>
#include <memory>
#include <vector>

#include "features/workspaces/komorebi_workarea.h"
#include "features/workspaces/testing/fake_workarea.h"
#include "kernel/qt/live_desktop.h"

namespace {

namespace kmb = waku::workspaces::komorebi;
using Slot = kmb::WorkAreaTimers::Slot;

QStringList& guardMessages() {
  static QStringList messages;
  return messages;
}

QtMessageHandler g_previousHandler = nullptr;

void collectGuardMessages(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  if (text.contains(QStringLiteral("komorebi work area"))) {
    guardMessages().append(text);
    return;
  }
  if (g_previousHandler) g_previousHandler(type, context, text);
}

// Collects the guard's log lines while it lives.
struct GuardMessageCapture {
  GuardMessageCapture() {
    guardMessages().clear();
    g_previousHandler = qInstallMessageHandler(collectGuardMessages);
  }
  ~GuardMessageCapture() { qInstallMessageHandler(g_previousHandler); }
  int count(const QString& part) const {
    int n = 0;
    for (const QString& line : guardMessages()) n += line.contains(part) ? 1 : 0;
    return n;
  }
};

}  // namespace

TEST(KomorebiWorkAreaGuard, OneBurstNotifiesAfterTheSettleAndAgainLate) {
  kmb::WorkAreaRig rig;
  for (int i = 0; i < 5; ++i) {
    rig.guard->screensChanged();
    rig.clock->advance(100);
  }
  // The last change was at 400 ms: nothing before 900 ms.
  rig.clock->advance(399);
  EXPECT_TRUE(rig.log->calls.empty());
  rig.clock->advance(1);
  EXPECT_EQ(rig.log->calls, std::vector<int>{900});
  rig.clock->advance(60000);
  EXPECT_EQ(rig.log->calls, (std::vector<int>{900, 5900}));
  EXPECT_FALSE(rig.clock->active(Slot::Settle));
  EXPECT_FALSE(rig.clock->active(Slot::Late));
}

TEST(KomorebiWorkAreaGuard, ASecondBurstDuringTheLateWaitRearmsThePair) {
  kmb::WorkAreaRig rig;
  rig.guard->screensChanged();
  rig.clock->advance(2000);
  EXPECT_EQ(rig.log->calls, std::vector<int>{500});
  rig.guard->screensChanged();
  rig.clock->advance(60000);
  // The first burst's late notification (5500) is superseded by the second burst's pair.
  EXPECT_EQ(rig.log->calls, (std::vector<int>{500, 2500, 7500}));
}

TEST(KomorebiWorkAreaGuard, NudgeNotifiesAtOnceAndLate) {
  kmb::WorkAreaRig rig;
  rig.guard->nudge();
  EXPECT_EQ(rig.log->calls, std::vector<int>{0});
  rig.clock->advance(60000);
  EXPECT_EQ(rig.log->calls, (std::vector<int>{0, 5000}));
}

TEST(KomorebiWorkAreaGuard, NudgeCancelsAPendingSettle) {
  kmb::WorkAreaRig rig;
  rig.guard->screensChanged();
  rig.clock->advance(100);
  rig.guard->nudge();
  rig.clock->advance(60000);
  EXPECT_EQ(rig.log->calls, (std::vector<int>{100, 5100}));
}

TEST(KomorebiWorkAreaGuard, AMissingWindowIsLoggedOncePerChange) {
  GuardMessageCapture capture;
  kmb::WorkAreaRig rig;
  rig.log->result = {};  // komorebi does not run
  rig.guard->nudge();
  rig.clock->advance(60000);
  rig.guard->nudge();
  rig.clock->advance(60000);
  EXPECT_EQ(rig.log->calls.size(), 4u);
  EXPECT_EQ(capture.count(QStringLiteral("no komorebi window")), 1);

  rig.log->result = {true, true, 0xabc, 0};
  rig.guard->nudge();
  EXPECT_EQ(capture.count(QStringLiteral("nudge sent to 0xabc")), 1);
  rig.log->result = {};
  rig.guard->nudge();
  EXPECT_EQ(capture.count(QStringLiteral("no komorebi window")), 2);
}

TEST(KomorebiWorkAreaGuard, ARefusedSendWarnsOncePerRun) {
  GuardMessageCapture capture;
  kmb::WorkAreaRig rig;
  rig.log->result = {true, false, 0xabc, 5};
  rig.guard->nudge();
  rig.clock->advance(60000);
  rig.guard->nudge();
  rig.clock->advance(60000);
  EXPECT_EQ(rig.log->calls.size(), 4u);
  EXPECT_EQ(capture.count(QStringLiteral("refused")), 1);
  EXPECT_EQ(capture.count(QStringLiteral("elevated")), 1);
}

TEST(KomorebiWorkAreaGuard, ScreenSignalsArmTheSettle) {
  kmb::WorkAreaRig rig;
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  emit screen->availableGeometryChanged(QRect(0, 40, 800, 560));
  EXPECT_TRUE(rig.clock->active(Slot::Settle));
  rig.clock->advance(60000);
  emit screen->geometryChanged(QRect(0, 0, 800, 600));
  EXPECT_TRUE(rig.clock->active(Slot::Settle));
  rig.clock->advance(60000);
  emit screen->logicalDotsPerInchChanged(144.0);
  EXPECT_TRUE(rig.clock->active(Slot::Settle));
  rig.clock->advance(60000);
  EXPECT_EQ(rig.log->calls, (std::vector<int>{500, 5500, 60500, 65500, 120500, 125500}));
}

TEST(KomorebiWorkAreaGuard, WithoutANotifierItIsInert) {
  auto now = std::make_shared<int>(0);
  auto timers = std::make_unique<kmb::FakeWorkAreaClock>(now);
  kmb::FakeWorkAreaClock* clock = timers.get();
  kmb::KomorebiWorkAreaGuard guard(nullptr, std::move(timers));
  EXPECT_FALSE(guard.active());
  guard.screensChanged();
  guard.nudge();
  emit QGuiApplication::primaryScreen() -> availableGeometryChanged(QRect(0, 0, 10, 10));
  EXPECT_FALSE(clock->active(Slot::Settle));
  EXPECT_FALSE(clock->active(Slot::Late));
}

TEST(KomorebiWorkAreaGuard, OffTheLiveDesktopTheRealGuardHasNoNotifier) {
  // Tests run on the offscreen platform; the flag switches it off on a live desktop too.
  EXPECT_FALSE(waku::kernel::liveDesktop(QStringLiteral("workarea_sync")));
  EXPECT_FALSE(kmb::makeKomorebiWorkAreaGuard()->active());
  waku::kernel::setCapabilityDisabled(QStringLiteral("workarea_sync"), true);
  EXPECT_FALSE(waku::kernel::liveDesktop(QStringLiteral("workarea_sync")));
  EXPECT_FALSE(kmb::makeKomorebiWorkAreaGuard()->active());
  waku::kernel::setCapabilityDisabled(QStringLiteral("workarea_sync"), false);
}

TEST(KomorebiWorkAreaGuard, TheQtTimersFireOnTheEventLoop) {
  auto log = std::make_shared<kmb::FakeWorkAreaLog>();
  auto now = std::make_shared<int>(0);
  auto timers = kmb::makeQtWorkAreaTimers();
  const kmb::WorkAreaTimers* qtTimers = timers.get();
  kmb::KomorebiWorkAreaGuard guard(std::make_unique<kmb::FakeWorkAreaNotifier>(log, now),
                                   std::move(timers), {10, 30});
  guard.screensChanged();
  guard.screensChanged();
  EXPECT_TRUE(QTest::qWaitFor([&] { return log->calls.size() == 2; }, 3000));
  // Both timers have fired and none is armed again: no third call can come.
  EXPECT_FALSE(qtTimers->active(Slot::Settle));
  EXPECT_FALSE(qtTimers->active(Slot::Late));
  EXPECT_EQ(log->calls.size(), 2u);
}

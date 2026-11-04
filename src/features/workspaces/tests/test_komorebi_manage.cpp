// KomorebiWorkAreaGuard with a manager: when it asks komorebi to reload after a display change.
// No test starts the real komorebic: the manager is a fake, and the real one exists only with
// --manage-komorebi on the live desktop.
#include <gtest/gtest.h>

#include <QString>
#include <memory>
#include <vector>

#include "features/workspaces/komorebi_workarea.h"
#include "features/workspaces/testing/fake_workarea.h"
#include "kernel/qt/live_desktop.h"

namespace {

namespace kmb = waku::workspaces::komorebi;
using Slot = kmb::WorkAreaTimers::Slot;

}  // namespace

TEST(KomorebiManage, ADisplayChangeReloadsAfterTheSettle) {
  kmb::WorkAreaRig rig(true);
  rig.guard->displayChanged();
  rig.clock->advance(499);
  EXPECT_TRUE(rig.manageLog->calls.empty());
  rig.clock->advance(1);
  EXPECT_EQ(rig.manageLog->calls, std::vector<int>{500});
}

TEST(KomorebiManage, ABurstReloadsOnce) {
  kmb::WorkAreaRig rig(true);
  for (int i = 0; i < 5; ++i) {
    rig.guard->displayChanged();
    rig.clock->advance(100);
  }
  rig.clock->advance(60000);
  EXPECT_EQ(rig.manageLog->calls, std::vector<int>{900});
}

TEST(KomorebiManage, AWorkAreaChangeAloneDoesNotReload) {
  kmb::WorkAreaRig rig(true);
  rig.guard->screensChanged();
  rig.clock->advance(60000);
  EXPECT_TRUE(rig.manageLog->calls.empty());
  EXPECT_EQ(rig.log->calls.size(), 2u);  // the work area nudge still runs
}

TEST(KomorebiManage, TheWorkAreaNudgeStillRunsAfterAReload) {
  kmb::WorkAreaRig rig(true);
  rig.guard->displayChanged();
  rig.clock->advance(60000);
  EXPECT_EQ(rig.manageLog->calls.size(), 1u);
  EXPECT_EQ(rig.log->calls, (std::vector<int>{500, 5500}));
}

TEST(KomorebiManage, ACooldownStopsAReloadFromStartingAnother) {
  kmb::WorkAreaRig rig(true);
  rig.guard->displayChanged();
  rig.clock->advance(1000);
  EXPECT_EQ(rig.manageLog->calls.size(), 1u);
  EXPECT_TRUE(rig.clock->active(Slot::Cooldown));
  // The reload moved a window: another display change a moment later asks again, in vain.
  rig.guard->displayChanged();
  rig.clock->advance(1000);
  EXPECT_EQ(rig.manageLog->calls.size(), 1u);
  // After the cooldown the next change reloads again.
  rig.clock->advance(30000);
  EXPECT_FALSE(rig.clock->active(Slot::Cooldown));
  rig.guard->displayChanged();
  rig.clock->advance(500);
  EXPECT_EQ(rig.manageLog->calls.size(), 2u);
}

TEST(KomorebiManage, NoBarMeansNoCooldownAndAnotherTryLater) {
  kmb::WorkAreaRig rig(true);
  rig.manageLog->result = {false, false};
  rig.guard->displayChanged();
  rig.clock->advance(1000);
  EXPECT_EQ(rig.manageLog->calls.size(), 1u);
  EXPECT_FALSE(rig.clock->active(Slot::Cooldown));
  rig.manageLog->result = {true, true};
  rig.guard->displayChanged();
  rig.clock->advance(500);
  EXPECT_EQ(rig.manageLog->calls.size(), 2u);
}

TEST(KomorebiManage, AFailedStartDoesNotStartTheCooldown) {
  kmb::WorkAreaRig rig(true);
  rig.manageLog->result = {true, false};
  rig.guard->displayChanged();
  rig.clock->advance(1000);
  EXPECT_FALSE(rig.clock->active(Slot::Cooldown));
}

TEST(KomorebiManage, WithoutAManagerADisplayChangeOnlyNudges) {
  kmb::WorkAreaRig rig(false);
  EXPECT_FALSE(rig.guard->managing());
  rig.guard->displayChanged();
  rig.clock->advance(500);
  EXPECT_EQ(rig.log->calls, std::vector<int>{500});
  EXPECT_TRUE(rig.manageLog->calls.empty());
}

TEST(KomorebiManage, AManagerAloneWorksWithoutANotifier) {
  kmb::WorkAreaRig rig(true, false);
  EXPECT_TRUE(rig.guard->managing());
  EXPECT_FALSE(rig.guard->active());
  rig.guard->displayChanged();
  rig.clock->advance(500);
  EXPECT_EQ(rig.manageLog->calls, std::vector<int>{500});
  EXPECT_TRUE(rig.log->calls.empty());
}

TEST(KomorebiManage, TheRealGuardHasNoManagerWithoutTheCapability) {
  // Off the live desktop (the offscreen platform of every test) the capability is never on.
  EXPECT_FALSE(kmb::makeKomorebiWorkAreaGuard()->managing());
}

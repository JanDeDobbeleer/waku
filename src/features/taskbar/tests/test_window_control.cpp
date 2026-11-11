#include <gtest/gtest.h>

#include <memory>

#include "features/taskbar/qml/window_control.h"
#include "features/taskbar/testing/fake_window_control.h"

using namespace waku::taskbar;

namespace {

struct Harness {
  explicit Harness(bool live = true) {
    fake = std::make_shared<FakeWindowControl>();
    fake->windows = {100, 200};
    fake->foregroundWindow = 100;
    control = std::make_unique<WindowControl>(fake, live, nullptr);
  }
  std::shared_ptr<FakeWindowControl> fake;
  std::unique_ptr<WindowControl> control;
};

}  // namespace

TEST(WindowControl, ActivatesAWindowAndReportsIt) {
  Harness h;
  EXPECT_TRUE(h.control->activate(200));
  EXPECT_EQ(h.control->foreground(), 200);
  EXPECT_EQ(h.fake->log, QStringList{QStringLiteral("front 200")});
}

TEST(WindowControl, RefusesAnInvalidWindow) {
  Harness h;
  EXPECT_FALSE(h.control->activate(999));
  EXPECT_FALSE(h.control->activate(0));
  EXPECT_FALSE(h.control->activate(-5));
  EXPECT_TRUE(h.fake->log.isEmpty());
  EXPECT_FALSE(h.control->isWindow(999));
  EXPECT_FALSE(h.control->isWindow(0));
  EXPECT_TRUE(h.control->isWindow(100));
}

TEST(WindowControl, RestoresAMinimisedWindowBeforeFocusingIt) {
  Harness h;
  h.fake->minimized = {200};
  EXPECT_TRUE(h.control->activate(200));
  EXPECT_EQ(h.fake->log, (QStringList{QStringLiteral("restore 200"), QStringLiteral("front 200")}));
}

TEST(WindowControl, DoesNotRestoreAWindowThatIsNotMinimised) {
  Harness h;
  EXPECT_TRUE(h.control->activate(100));
  EXPECT_EQ(h.fake->log, QStringList{QStringLiteral("front 100")});
}

TEST(WindowControl, ReturnsFalseWhenTheForegroundLockRefuses) {
  Harness h;
  h.fake->refuseForeground = true;
  EXPECT_FALSE(h.control->activate(200));
  EXPECT_EQ(h.control->foreground(), 100);
}

TEST(WindowControl, ReadsTheForegroundAndZeroWhenNone) {
  Harness h;
  EXPECT_EQ(h.control->foreground(), 100);
  h.fake->foregroundWindow = 0;
  EXPECT_EQ(h.control->foreground(), 0);
}

TEST(WindowControl, ReadsTheMonitorOfAnyValidWindow) {
  Harness h;
  h.fake->monitors = {{100, 11}, {200, 22}};
  EXPECT_EQ(h.control->monitor(h.control->foreground()), 11);
  EXPECT_EQ(h.control->monitor(200), 22);
  EXPECT_EQ(h.control->monitor(999), 0);
  EXPECT_EQ(h.control->monitor(0), 0);
  EXPECT_EQ(h.control->monitor(-1), 0);
  h.fake->monitors.erase(100);
  EXPECT_EQ(h.control->monitor(100), 0);
}

TEST(WindowControl, IsInertWithoutALiveDesktop) {
  Harness h(false);
  EXPECT_FALSE(h.control->activate(200));
  EXPECT_EQ(h.control->foreground(), 0);
  EXPECT_EQ(h.control->monitor(100), 0);
  EXPECT_FALSE(h.control->isWindow(100));
  EXPECT_TRUE(h.fake->log.isEmpty());
}

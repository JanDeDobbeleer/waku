#include <gtest/gtest.h>

#include <vector>

#include "features/network/throughput.h"

using namespace wabar::network;

namespace {

InterfaceCounters iface(quint64 id, quint64 rx, quint64 tx, bool up = true,
                        const char* name = "Ethernet") {
  InterfaceCounters counters;
  counters.id = id;
  counters.name = QString::fromLatin1(name);
  counters.isUp = up;
  counters.rxBytes = rx;
  counters.txBytes = tx;
  return counters;
}

}  // namespace

TEST(ThroughputTracker, StartsAtZero) {
  ThroughputTracker tracker;
  EXPECT_EQ(tracker.rxBytesPerSecond(), 0);
  EXPECT_EQ(tracker.txBytesPerSecond(), 0);
  EXPECT_EQ(tracker.rxBytesTotal(), 0);
  EXPECT_TRUE(tracker.adapters().empty());
}

TEST(ThroughputTracker, TheFirstReadOnlySetsTheBaseline) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 5000, 3000)}, 1.0);
  EXPECT_EQ(tracker.rxBytesPerSecond(), 0);
  EXPECT_EQ(tracker.rxBytesTotal(), 0);
  ASSERT_EQ(tracker.adapters().size(), 1u);
  EXPECT_EQ(tracker.adapters()[0].rxBytesPerSecond, 0);
}

TEST(ThroughputTracker, RateIsTheDeltaOverTheElapsedTime) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 1000, 500)}, 0);
  tracker.update({iface(1, 3000, 1000)}, 2.0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 1000);
  EXPECT_DOUBLE_EQ(tracker.txBytesPerSecond(), 250);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 2000);
  EXPECT_DOUBLE_EQ(tracker.txBytesTotal(), 500);

  tracker.update({iface(1, 3000, 1000)}, 1.0);  // idle second
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 2000);
}

TEST(ThroughputTracker, SumsTheInterfacesThatAreUpAndListsAll) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 0, 0, true, "Wi-Fi"), iface(2, 0, 0, false, "Docker")}, 0);
  tracker.update({iface(1, 400, 100, true, "Wi-Fi"), iface(2, 900, 900, false, "Docker")}, 1.0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 400);
  EXPECT_DOUBLE_EQ(tracker.txBytesPerSecond(), 100);
  ASSERT_EQ(tracker.adapters().size(), 2u);
  EXPECT_EQ(tracker.adapters()[0].name, QStringLiteral("Wi-Fi"));
  EXPECT_TRUE(tracker.adapters()[0].isUp);
  EXPECT_DOUBLE_EQ(tracker.adapters()[0].rxBytesPerSecond, 400);
  EXPECT_EQ(tracker.adapters()[1].name, QStringLiteral("Docker"));
  EXPECT_FALSE(tracker.adapters()[1].isUp);
  EXPECT_DOUBLE_EQ(tracker.adapters()[1].rxBytesPerSecond, 900);
}

TEST(ThroughputTracker, ACounterBeyondThirtyTwoBitsDoesNotWrap) {
  constexpr quint64 kBig = 0xFFFFFFF0ull;  // just under the 32-bit limit
  ThroughputTracker tracker;
  tracker.update({iface(1, kBig, kBig)}, 0);
  tracker.update({iface(1, kBig + 0x100, kBig + 0x200)}, 1.0);  // crosses 2^32
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 256);
  EXPECT_DOUBLE_EQ(tracker.txBytesPerSecond(), 512);
}

TEST(ThroughputTracker, ACounterResetCountsWhatItShowsNow) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 1'000'000, 2'000'000)}, 0);
  tracker.update({iface(1, 1'000'500, 2'000'500)}, 1.0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 500);

  tracker.update({iface(1, 300, 700)}, 1.0);  // the adapter restarted from zero
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 300);
  EXPECT_DOUBLE_EQ(tracker.txBytesPerSecond(), 700);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 800);

  tracker.update({iface(1, 400, 800)}, 1.0);  // and goes on from there
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 100);
}

TEST(ThroughputTracker, ADisappearingInterfaceKeepsItsTotalAndLeavesTheRate) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 0, 0), iface(2, 0, 0, true, "USB")}, 0);
  tracker.update({iface(1, 100, 0), iface(2, 1000, 0, true, "USB")}, 1.0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 1100);

  tracker.update({iface(1, 150, 0)}, 1.0);  // the USB adapter was unplugged
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 50);
  ASSERT_EQ(tracker.adapters().size(), 1u);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 1150);

  // It comes back with a fresh counter: a new baseline, no spike.
  tracker.update({iface(1, 200, 0), iface(2, 5000, 0, true, "USB")}, 1.0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 50);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 1200);
}

TEST(ThroughputTracker, AnEmptyReadZeroesTheRates) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 0, 0)}, 0);
  tracker.update({iface(1, 100, 100)}, 1.0);
  tracker.update({}, 1.0);  // the query failed
  EXPECT_EQ(tracker.rxBytesPerSecond(), 0);
  EXPECT_TRUE(tracker.adapters().empty());
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 100);
}

TEST(ThroughputTracker, ResetBaselineDropsAPauseButKeepsTheTotals) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 0, 0)}, 0);
  tracker.update({iface(1, 100, 0)}, 1.0);
  tracker.resetBaseline();
  tracker.clearRates();
  EXPECT_EQ(tracker.rxBytesPerSecond(), 0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 100);

  tracker.update({iface(1, 900'000, 0)}, 0);  // a long pause: nothing counted
  tracker.update({iface(1, 900'050, 0)}, 1.0);
  EXPECT_DOUBLE_EQ(tracker.rxBytesPerSecond(), 50);
  EXPECT_DOUBLE_EQ(tracker.rxBytesTotal(), 150);
}

TEST(ThroughputTracker, AZeroIntervalCountsNothing) {
  ThroughputTracker tracker;
  tracker.update({iface(1, 0, 0)}, 0);
  tracker.update({iface(1, 100, 0)}, 0);
  EXPECT_EQ(tracker.rxBytesPerSecond(), 0);
  EXPECT_EQ(tracker.rxBytesTotal(), 0);
}

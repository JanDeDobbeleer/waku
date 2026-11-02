#include <gtest/gtest.h>

#include <vector>

#include "features/workspaces/backoff.h"

using waku::workspaces::nextBackoffMs;

TEST(WorkspacesBackoff, DoublesUpToTheCap) {
  int wait = 4000;
  std::vector<int> seen;
  for (int i = 0; i < 6; ++i) {
    seen.push_back(wait);
    wait = nextBackoffMs(wait, 30000);
  }
  EXPECT_EQ(seen, (std::vector<int>{4000, 8000, 16000, 30000, 30000, 30000}));
}

TEST(WorkspacesBackoff, ACapBelowTheWaitKeepsTheWaitConstant) {
  EXPECT_EQ(nextBackoffMs(5000, 5000), 5000);
  EXPECT_EQ(nextBackoffMs(5000, 100), 5000);
}

TEST(WorkspacesBackoff, NeverOverflows) {
  EXPECT_EQ(nextBackoffMs(2000000000, 2100000000), 2100000000);
}

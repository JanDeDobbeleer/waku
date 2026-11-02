#include <gtest/gtest.h>

#include "kernel/win32/windows_build.h"

TEST(KernelWindowsBuild, IsASaneBuildNumberOfWindows10OrLater) {
  const unsigned build = waku::kernel::windowsBuild();
  EXPECT_GE(build, 10240u);  // Windows 10 1507, the first build Qt 6 runs on
  EXPECT_LT(build, 100000u);
}

TEST(KernelWindowsBuild, IsCachedAndStable) {
  EXPECT_EQ(waku::kernel::windowsBuild(), waku::kernel::windowsBuild());
}

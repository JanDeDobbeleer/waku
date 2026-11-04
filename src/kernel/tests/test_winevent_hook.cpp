// WinEventHook: the parts that need no hook. A real install is exercised by the taskbar tracker
// and the tiler on the live desktop; no test hooks the machine.
#include <gtest/gtest.h>

#include <utility>

#include "kernel/win32/winevent_hook.h"

TEST(KernelWinEventHook, AnEmptyHookIsNotInstalledAndUninstallIsSafe) {
  waku::kernel::WinEventHook hook;
  EXPECT_FALSE(hook.installed());
  hook.uninstall();
  EXPECT_FALSE(hook.installed());
}

TEST(KernelWinEventHook, MovingAnEmptyHookKeepsItEmpty) {
  waku::kernel::WinEventHook first;
  waku::kernel::WinEventHook second(std::move(first));
  EXPECT_FALSE(first.installed());
  EXPECT_FALSE(second.installed());
  first = std::move(second);
  EXPECT_FALSE(first.installed());
}

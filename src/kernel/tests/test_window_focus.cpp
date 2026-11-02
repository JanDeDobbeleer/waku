// forceForegroundFocus has one testable edge: a null window is ignored and nothing is touched.
#include <gtest/gtest.h>

#include "kernel/win32/window_focus.h"

TEST(KernelWindowFocus, ANullWindowIsIgnored) {
  waku::kernel::forceForegroundFocus(0);
  SUCCEED();
}

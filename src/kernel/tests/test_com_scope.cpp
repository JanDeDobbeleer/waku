#include <windows.h>
#include <gtest/gtest.h>
#include <objbase.h>

#include <thread>

#include "kernel/win32/com_scope.h"

using waku::kernel::ComApartment;
using waku::kernel::ComScope;

namespace {

// Runs `body` on a fresh thread, which starts without an apartment of its own.
template <typename Body>
void onFreshThread(Body body) {
  std::thread(body).join();
}

}  // namespace

TEST(ComScope, JoinsAndLeavesTheApartment) {
  onFreshThread([] {
    {
      const ComScope com(ComApartment::SingleThreaded);
      EXPECT_TRUE(com.initialised());
      EXPECT_EQ(com.result(), S_OK);
      APTTYPE type{};
      APTTYPEQUALIFIER qualifier{};
      ASSERT_EQ(CoGetApartmentType(&type, &qualifier), S_OK);
      EXPECT_TRUE(type == APTTYPE_STA || type == APTTYPE_MAINSTA);
    }
    // The STA is gone, so the thread can now join the MTA.
    const ComScope mta(ComApartment::MultiThreaded);
    EXPECT_EQ(mta.result(), S_OK);
  });
}

TEST(ComScope, NestedScopeOfTheSameKindIsCounted) {
  onFreshThread([] {
    const ComScope outer(ComApartment::MultiThreaded);
    {
      const ComScope inner(ComApartment::MultiThreaded);
      EXPECT_TRUE(inner.initialised());
      EXPECT_EQ(inner.result(), S_FALSE);
    }
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    ASSERT_EQ(CoGetApartmentType(&type, &qualifier), S_OK);
    EXPECT_EQ(type, APTTYPE_MTA);
  });
}

TEST(ComScope, OtherApartmentIsKeptAndNotUndone) {
  onFreshThread([] {
    const ComScope mta(ComApartment::MultiThreaded);
    {
      const ComScope sta(ComApartment::SingleThreaded, true);
      EXPECT_FALSE(sta.initialised());
      EXPECT_EQ(sta.result(), RPC_E_CHANGED_MODE);
    }
    // The thread is still in the MTA: a second MTA scope only counts.
    const ComScope again(ComApartment::MultiThreaded);
    EXPECT_EQ(again.result(), S_FALSE);
  });
}

#include <gtest/gtest.h>

#include <QApplication>
#include <QPointer>
#include <memory>

#include "app/runtime.h"

namespace {

class RuntimeTest : public testing::Test {};

}  // namespace

TEST_F(RuntimeTest, OwnsIndependentServiceInstancesAndWidgetRegistry) {
  yasb::app::Runtime first;
  yasb::app::Runtime second;

  EXPECT_NE(&first.eventBus(), &second.eventBus());
  EXPECT_NE(&first.globalState(), &second.globalState());
  EXPECT_NE(&first.widgetRegistry(), &second.widgetRegistry());
  EXPECT_TRUE(first.widgetRegistry().contains("yasb.cpu.CpuWidget"));
  EXPECT_TRUE(first.widgetRegistry().contains("yasb.memory.MemoryWidget"));
  EXPECT_TRUE(first.widgetRegistry().contains("yasb.battery.BatteryWidget"));
  EXPECT_TRUE(second.widgetRegistry().contains("yasb.cpu.CpuWidget"));
}

TEST_F(RuntimeTest, TransitionEngineIsDestroyedWithRuntimeWhileApplicationIsAlive) {
  QPointer<yasb::css::TransitionEngine> engine;
  {
    yasb::app::Runtime runtime;
    engine = &runtime.transitionEngine();
    ASSERT_FALSE(engine.isNull());
  }
  EXPECT_TRUE(engine.isNull());
}

TEST_F(RuntimeTest, PollingWorkersAreSharedOnlyForMatchingSourceAndInterval) {
  yasb::app::Runtime runtime;
  int factories = 0;
  const auto factory = [&factories] {
    ++factories;
    return [] { return QVariant(1); };
  };

  auto* first = runtime.pollingWorkers().get("cpu", 1000, factory);
  auto* sameCadence = runtime.pollingWorkers().get("cpu", 1000, factory);
  auto* differentCadence = runtime.pollingWorkers().get("cpu", 2000, factory);
  auto* differentSource = runtime.pollingWorkers().get("memory", 1000, factory);

  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first, sameCadence);
  EXPECT_NE(first, differentCadence);
  EXPECT_NE(first, differentSource);
  EXPECT_EQ(factories, 3);
  EXPECT_EQ(runtime.pollingWorkers().size(), 3u);
  EXPECT_EQ(runtime.pollingWorkers().get("cpu", 0, factory), nullptr);
  EXPECT_EQ(factories, 3);
}

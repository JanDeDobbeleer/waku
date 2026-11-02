#include <gtest/gtest.h>

#include <QTest>
#include <functional>
#include <memory>

#include "features/system_stats/gpu_engines.h"
#include "features/system_stats/qml/stat_sources.h"
#include "features/system_stats/testing/fake_stat_sources.h"

using namespace waku::system_stats;
using namespace waku::system_stats::testing;

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

std::string instance(const char* pid, const char* luid, int engine, const char* type) {
  return std::string("pid_") + pid + "_luid_0x00000000_" + luid + "_phys_0_eng_" +
         std::to_string(engine) + "_engtype_" + type;
}

}  // namespace

TEST(GpuEngines, KeyJoinsTheAdapterAndTheEngineType) {
  EXPECT_EQ(gpuEngineKey(instance("12", "0x0000A1B2", 3, "3D")), "luid_0x00000000_0x0000A1B2|3D");
  EXPECT_EQ(gpuEngineKey(instance("99", "0x0000A1B2", 7, "VideoDecode")),
            "luid_0x00000000_0x0000A1B2|VideoDecode");
  EXPECT_TRUE(gpuEngineKey("garbage").empty());
  EXPECT_TRUE(gpuEngineKey("pid_1_luid_0x0_0x1_phys_0_eng_0").empty());
}

TEST(GpuEngines, ProcessesOnOneEngineTypeAddUp) {
  const double usage = gpuUsage({{instance("1", "0xA", 0, "3D"), 10.5},
                                 {instance("2", "0xA", 0, "3D"), 20.0},
                                 {instance("3", "0xA", 1, "3D"), 5.0}});
  EXPECT_DOUBLE_EQ(usage, 35.5);
}

TEST(GpuEngines, TheBusiestEngineTypeWins) {
  const double usage = gpuUsage({{instance("1", "0xA", 0, "3D"), 10.0},
                                 {instance("1", "0xA", 2, "Copy"), 40.0},
                                 {instance("1", "0xA", 3, "VideoDecode"), 25.0}});
  EXPECT_DOUBLE_EQ(usage, 40.0);
}

TEST(GpuEngines, AdaptersAreKeptApart) {
  const double usage =
      gpuUsage({{instance("1", "0xA", 0, "3D"), 30.0}, {instance("1", "0xB", 0, "3D"), 45.0}});
  EXPECT_DOUBLE_EQ(usage, 45.0);
}

TEST(GpuEngines, ClampsAndIgnoresNoise) {
  EXPECT_DOUBLE_EQ(gpuUsage({}), 0.0);
  EXPECT_DOUBLE_EQ(gpuUsage({{"garbage", 50.0}, {instance("1", "0xA", 0, "3D"), -3.0}}), 0.0);
  EXPECT_DOUBLE_EQ(
      gpuUsage({{instance("1", "0xA", 0, "3D"), 80.0}, {instance("2", "0xA", 0, "3D"), 80.0}}),
      100.0);
}

TEST(GpuSource, ExposesUsageMemoryAndName) {
  Gpu gpu(std::make_shared<FakeGpu>(), nullptr);
  gpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return gpu.ready(); }));
  EXPECT_TRUE(gpu.available());
  EXPECT_DOUBLE_EQ(gpu.percent(), 35);
  EXPECT_EQ(gpu.memory(), 2'000'000'000LL);
  EXPECT_EQ(gpu.memoryTotal(), 8'000'000'000LL);
  EXPECT_DOUBLE_EQ(gpu.memoryPercent(), 25);
  EXPECT_EQ(gpu.name(), QStringLiteral("Fake GPU"));
}

TEST(GpuSource, MissingCountersMeanUnavailableAndZero) {
  auto fake = std::make_shared<FakeGpu>();
  fake->present = false;
  Gpu gpu(fake, nullptr);
  gpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return gpu.ready(); }));
  EXPECT_FALSE(gpu.available());
}

TEST(DiskSource, ListsVolumesAndTheSystemDrive) {
  Disks disks(std::make_shared<FakeDisks>(), nullptr);
  disks.componentComplete();
  ASSERT_TRUE(waitFor([&] { return disks.ready(); }));
  const QVariantList list = disks.disks();
  ASSERT_EQ(list.size(), 2);
  const QVariantMap c = list[0].toMap();
  EXPECT_EQ(c.value(QStringLiteral("mount")).toString(), QStringLiteral("C:\\"));
  EXPECT_EQ(c.value(QStringLiteral("name")).toString(), QStringLiteral("Windows"));
  EXPECT_EQ(c.value(QStringLiteral("total")).toLongLong(), 500'000'000'000LL);
  EXPECT_EQ(c.value(QStringLiteral("free")).toLongLong(), 125'000'000'000LL);
  EXPECT_EQ(c.value(QStringLiteral("used")).toLongLong(), 375'000'000'000LL);
  EXPECT_DOUBLE_EQ(c.value(QStringLiteral("perc")).toDouble(), 75);
  EXPECT_TRUE(c.value(QStringLiteral("system")).toBool());
  EXPECT_FALSE(list[1].toMap().value(QStringLiteral("system")).toBool());
  EXPECT_EQ(disks.primaryDisk().value(QStringLiteral("mount")).toString(), QStringLiteral("C:\\"));
}

TEST(DiskSource, RealVolumesAreReadWithoutThrowing) {
  // The real source only reads free space; it is safe on any machine and on the offscreen platform.
  auto source = createDiskDataSource();
  const std::vector<DiskSample> samples = source->sample();
  for (const DiskSample& disk : samples) {
    EXPECT_GT(disk.total, 0u);
    EXPECT_LE(disk.used, disk.total);
    EXPECT_GE(disk.percent, 0.0);
    EXPECT_LE(disk.percent, 100.0);
  }
}

#include <gtest/gtest.h>

#include <QTest>
#include <cmath>
#include <functional>
#include <memory>

#include "features/system_stats/qml/stat_sources.h"
#include "features/system_stats/temperature.h"
#include "features/system_stats/testing/fake_stat_sources.h"

using namespace waku::system_stats;
using namespace waku::system_stats::testing;

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

ThermalZone zone(const char* name, double deciKelvin, double kelvin = 0) {
  return {name, deciKelvin, kelvin};
}

}  // namespace

TEST(Temperature, DeciKelvinConvertsToCelsius) {
  EXPECT_NEAR(*celsiusFromDeciKelvin(3228), 49.65, 1e-9);  // 322.8 K
  EXPECT_NEAR(*celsiusFromDeciKelvin(3031.5), 30.0, 1e-9);
  EXPECT_NEAR(*celsiusFromDeciKelvin(3731.5), 100.0, 1e-9);
}

TEST(Temperature, ImplausibleReadingsGiveNothing) {
  EXPECT_FALSE(celsiusFromDeciKelvin(0));
  EXPECT_FALSE(celsiusFromDeciKelvin(-5));
  EXPECT_FALSE(celsiusFromDeciKelvin(2330));  // -40 C placeholder of a sensorless zone
  EXPECT_FALSE(celsiusFromDeciKelvin(2730));  // 0 C
  EXPECT_FALSE(celsiusFromDeciKelvin(4300));  // above 150 C
  EXPECT_FALSE(celsiusFromDeciKelvin(std::nan("")));
}

TEST(Temperature, ZoneUsesWholeKelvinWhenThePreciseCounterIsEmpty) {
  EXPECT_NEAR(*zoneCelsius(zone("a", 3228, 321)), 49.65, 1e-9);  // precise wins
  EXPECT_NEAR(*zoneCelsius(zone("a", 0, 321)), 47.85, 1e-9);     // whole Kelvin fallback
  EXPECT_FALSE(zoneCelsius(zone("a", 0, 0)));
}

TEST(Temperature, CpuTemperatureIsTheHottestPlausibleZone) {
  const std::vector<ThermalZone> zones = {zone("\\_SB.TZ0", 3228), zone("\\_SB.TZ99", 3244),
                                          zone("\\_SB.TZ33", 2330), zone("\\TPOL", 0),
                                          zone("\\_SB.TZ5", 0, 319)};
  EXPECT_NEAR(*selectCpuTemperature(zones), 51.25, 1e-9);  // 324.4 K
}

TEST(Temperature, CpuTemperatureIsUnavailableWithoutZones) {
  EXPECT_FALSE(selectCpuTemperature({}));
  EXPECT_FALSE(selectCpuTemperature({zone("\\TPOL", 0), zone("\\_SB.TZ33", 2330)}));
}

TEST(Temperature, DeciCelsiusZeroMeansTheDriverReportsNone) {
  EXPECT_FALSE(celsiusFromDeciCelsius(0));
  EXPECT_DOUBLE_EQ(*celsiusFromDeciCelsius(615), 61.5);
  EXPECT_FALSE(celsiusFromDeciCelsius(9000));
}

TEST(CpuTemperature, ExposesCelsiusAndAvailability) {
  Cpu cpu(std::make_shared<FakeCpu>(), nullptr);
  EXPECT_FALSE(cpu.temperatureAvailable());
  EXPECT_TRUE(std::isnan(cpu.temperature()));  // before the first sample: unknown, not 0
  cpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return cpu.ready(); }));
  EXPECT_TRUE(cpu.temperatureAvailable());
  EXPECT_DOUBLE_EQ(cpu.temperature(), 54.5);
}

TEST(CpuTemperature, MissingCountersMeanUnavailableAndNaN) {
  auto fake = std::make_shared<FakeCpu>();
  fake->hasTemperature = false;
  Cpu cpu(fake, nullptr);
  cpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return cpu.ready(); }));
  EXPECT_FALSE(cpu.temperatureAvailable());
  EXPECT_TRUE(std::isnan(cpu.temperature()));
  EXPECT_DOUBLE_EQ(cpu.percent(), 12.5);  // the rest of the sample still applies
}

TEST(CpuTemperature, DoesNotSampleWhileUnobserved) {
  auto fake = std::make_shared<FakeCpu>();
  Cpu cpu(fake, nullptr);
  cpu.setActive(false);
  cpu.componentComplete();
  // No timer and no sample on the pool: nothing can run later either.
  EXPECT_FALSE(cpu.polling());
  EXPECT_FALSE(cpu.sampling());
  EXPECT_EQ(fake->calls.load(), 0);
  EXPECT_FALSE(cpu.temperatureAvailable());
}

TEST(GpuTemperature, ExposesCelsiusAndAvailability) {
  Gpu gpu(std::make_shared<FakeGpu>(), nullptr);
  EXPECT_TRUE(std::isnan(gpu.temperature()));
  gpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return gpu.ready(); }));
  EXPECT_TRUE(gpu.temperatureAvailable());
  EXPECT_DOUBLE_EQ(gpu.temperature(), 61.0);
}

TEST(GpuTemperature, DriverWithoutTemperatureIsUnavailable) {
  auto fake = std::make_shared<FakeGpu>();
  fake->hasTemperature = false;
  Gpu gpu(fake, nullptr);
  gpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return gpu.ready(); }));
  EXPECT_TRUE(gpu.available());
  EXPECT_FALSE(gpu.temperatureAvailable());
  EXPECT_TRUE(std::isnan(gpu.temperature()));
}

TEST(GpuTemperature, DoesNotSampleWhileUnobserved) {
  auto fake = std::make_shared<FakeGpu>();
  Gpu gpu(fake, nullptr);
  gpu.setActive(false);
  gpu.componentComplete();
  EXPECT_FALSE(gpu.polling());
  EXPECT_FALSE(gpu.sampling());
  EXPECT_EQ(fake->calls.load(), 0);
}

TEST(RealTemperature, SourcesReadWithoutThrowing) {
  // The tests run on the offscreen platform: kernel::liveDesktop() is false, so the real sources
  // touch no thermal counter and no adapter and report nothing.
  const CpuSample cpu = createCpuDataSource()->sample();
  EXPECT_FALSE(cpu.temperature);
  const GpuSample gpu = createGpuDataSource()->sample();
  EXPECT_FALSE(gpu.temperature);
}

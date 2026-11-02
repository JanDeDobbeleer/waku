#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include "features/system_stats/system_stats.h"

namespace waku::system_stats {

class WindowsBatteryDataSource final : public BatteryDataSource {
 public:
  // Every widget shares this source: a second call within a second returns the first sample.
  std::optional<BatterySample> sample() override;

 private:
  std::optional<BatterySample> read();

  std::mutex mutex_;
  std::wstring devicePath_;
  bool hasLast_ = false;
  std::optional<BatterySample> last_;
  std::chrono::steady_clock::time_point lastAt_;
};

}  // namespace waku::system_stats

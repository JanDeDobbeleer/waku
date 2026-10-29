#pragma once

#include "features/system_stats/system_stats.h"

namespace wabar::system_stats {

class WindowsBatteryDataSource final : public BatteryDataSource {
 public:
  std::optional<BatterySample> sample() override;

 private:
  std::wstring devicePath_;
};

}  // namespace wabar::system_stats

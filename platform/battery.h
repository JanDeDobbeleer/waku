#pragma once

#include "platform/system_stats.h"

namespace yasb::platform {

class WindowsBatteryDataSource final : public BatteryDataSource {
 public:
  std::optional<BatterySample> sample() override;

 private:
  std::wstring devicePath_;
};

}  // namespace yasb::platform

#pragma once

#include "platform/system_stats.h"

namespace yasb::platform {

class WindowsMemoryDataSource final : public MemoryDataSource {
 public:
  MemorySample sample() override;
};

}  // namespace yasb::platform

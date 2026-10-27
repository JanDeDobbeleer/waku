#pragma once

#include <memory>

#include "platform/system_stats.h"

namespace yasb::platform {

class WindowsMemoryDataSource final : public MemoryDataSource {
 public:
  WindowsMemoryDataSource();
  ~WindowsMemoryDataSource() override;

  MemorySample sample() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yasb::platform

#pragma once

#include <memory>

#include "features/system_stats/system_stats.h"

namespace wabar::system_stats {

class WindowsMemoryDataSource final : public MemoryDataSource {
 public:
  WindowsMemoryDataSource();
  ~WindowsMemoryDataSource() override;

  MemorySample sample() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wabar::system_stats

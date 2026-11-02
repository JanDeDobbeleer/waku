#pragma once

#include <memory>

#include "features/system_stats/system_stats.h"

namespace waku::system_stats {

class WindowsMemoryDataSource final : public MemoryDataSource {
 public:
  WindowsMemoryDataSource();
  ~WindowsMemoryDataSource() override;

  MemorySample sample() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace waku::system_stats

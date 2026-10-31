#pragma once

#include <memory>

#include "features/system_stats/system_stats.h"

namespace wabar::system_stats {

class PdhCpuDataSource final : public CpuDataSource {
 public:
  PdhCpuDataSource();
  ~PdhCpuDataSource() override;
  CpuSample sample() override;

 private:
  CpuSample collect();  // under impl_->mutex

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wabar::system_stats

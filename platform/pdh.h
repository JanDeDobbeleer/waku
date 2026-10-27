#pragma once

#include <memory>

#include "platform/system_stats.h"

namespace yasb::platform {

class PdhCpuDataSource final : public CpuDataSource {
 public:
  PdhCpuDataSource();
  ~PdhCpuDataSource() override;
  CpuSample sample() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yasb::platform

#pragma once

#include <algorithm>

namespace waku::workspaces {

// The wait after a failed probe: doubled each time, never above `maxMs`, and never below the wait
// just used (a `maxMs` under `currentMs` keeps the wait constant). Both window manager clients use
// it while their manager is absent, so an idle bar without one wakes a few times a minute.
inline int nextBackoffMs(int currentMs, int maxMs) {
  if (currentMs <= 0) return currentMs;
  const long long doubled = static_cast<long long>(currentMs) * 2;
  return static_cast<int>(std::min<long long>(doubled, std::max(maxMs, currentMs)));
}

}  // namespace waku::workspaces

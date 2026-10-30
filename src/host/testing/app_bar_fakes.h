#pragma once

// A fake of the system side of an app bar (ABM_QUERYPOS and ABM_SETPOS): it holds the coordinates
// other app bars (or the Windows taskbar) already took and adjusts a request the way the shell
// does, pushing the requested edge inward. Tests only; nothing here talks to the desktop.

#include <algorithm>
#include <limits>
#include <vector>

#include "host/app_bar.h"

namespace wabar::host {

class FakeAppBarSystem final : public IAppBarSystem {
 public:
  struct Call {
    const char* what;  // "query" or "set"
    AppBarEdge edge;
    AppBarRect rect;
  };

  // The free region's limits in physical desktop coordinates. A bar cannot reach past them: a top
  // bar starts at `topLimit` or lower, a bottom bar ends at `bottomLimit` or higher, and so on.
  int topLimit = std::numeric_limits<int>::min();
  int bottomLimit = std::numeric_limits<int>::max();
  int leftLimit = std::numeric_limits<int>::min();
  int rightLimit = std::numeric_limits<int>::max();
  // ABM_SETPOS pushes the edge this many pixels further in than ABM_QUERYPOS did.
  int setExtra = 0;
  std::vector<Call> calls;

  AppBarRect queryPos(AppBarEdge edge, const AppBarRect& proposed) override {
    calls.push_back({"query", edge, proposed});
    return adjust(edge, proposed, 0);
  }

  AppBarRect setPos(AppBarEdge edge, const AppBarRect& proposed) override {
    calls.push_back({"set", edge, proposed});
    return adjust(edge, proposed, setExtra);
  }

 private:
  AppBarRect adjust(AppBarEdge edge, AppBarRect rect, int extra) const {
    switch (edge) {
      case AppBarEdge::Top:
        rect.top = std::max(
            rect.top, topLimit == std::numeric_limits<int>::min() ? rect.top : topLimit + extra);
        break;
      case AppBarEdge::Bottom:
        rect.bottom = std::min(rect.bottom, bottomLimit == std::numeric_limits<int>::max()
                                                ? rect.bottom
                                                : bottomLimit - extra);
        break;
      case AppBarEdge::Left:
        rect.left =
            std::max(rect.left,
                     leftLimit == std::numeric_limits<int>::min() ? rect.left : leftLimit + extra);
        break;
      case AppBarEdge::Right:
        rect.right = std::min(rect.right, rightLimit == std::numeric_limits<int>::max()
                                              ? rect.right
                                              : rightLimit - extra);
        break;
    }
    return rect;
  }
};

}  // namespace wabar::host

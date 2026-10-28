#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace tidy {

struct ScreenRef {
  QString name;  // e.g. "\\.\DISPLAY2"
  bool primary = false;
};

// Indices into `screens` selected by a Bar's `screens` patterns, in screen order, each once:
//   "primary"  the primary screen          "all"  every screen
//   otherwise  a screen whose name equals the pattern, or ends with it after a backslash
//              ("DISPLAY2" matches "\\.\DISPLAY2"); case-insensitive.
// Patterns that match nothing are ignored. An empty pattern list selects nothing.
std::vector<int> matchScreens(const QStringList& patterns, const std::vector<ScreenRef>& screens);

}  // namespace tidy

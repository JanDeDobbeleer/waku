#pragma once

#include <QString>

namespace waku::keymap {

// True when `name` and `process` are the same executable: case does not matter and `.exe` may be
// left out on either side ("Firefox" matches "firefox.exe").
bool processNamesMatch(const QString& name, const QString& process);

// How `waku:activate-or-launch` finds a window: `target` names the window's executable (see
// processNamesMatch), or else it is a part of the window title (case does not matter).
bool windowMatchesTarget(const QString& target, const QString& processName, const QString& title);

}  // namespace waku::keymap

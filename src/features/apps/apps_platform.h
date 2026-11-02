#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "features/apps/catalog.h"

namespace waku::apps {

// What a launch does.
enum class LaunchMode {
  Normal,        // start the app, or raise its window
  Admin,         // start a shortcut elevated; Store apps have no such path
  OpenLocation,  // show the shortcut's target in Explorer; Store apps have no file
};

// Starts an app. True when the shell accepted the request. GUI thread. Real: win32/launch_win.cpp.
// Tests pass a fake: no test ever starts a real app.
using AppLauncher = std::function<bool(const AppRecord&, LaunchMode)>;

// The Windows implementations (win32/).
AppLauncher makeWin32Launcher();
ShortcutResolver makeWin32ShortcutResolver();
AppsFolderEnumerator makeWin32AppsFolderEnumerator();
// FOLDERID_Programs (the user's Start Menu programs) then FOLDERID_CommonPrograms (all users'),
// those that exist.
QStringList startMenuFolders();

}  // namespace waku::apps

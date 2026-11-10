#pragma once

#include <functional>
#include <memory>

#include "features/tray/tray_host.h"

namespace waku::tray {

// How `createTrayHost` builds the host; the default is the real one, tests count calls instead of
// touching the desktop.
struct TrayHostFactories {
  // The window host (win32/tray_window_host.h); null for a host that is not valid (another waku of
  // the session has it).
  std::function<std::unique_ptr<TrayHost>()> makeWindow;
};

// The factories wired to the real window host.
TrayHostFactories realTrayHostFactories();

// The tray host of a run: a Shell_TrayWnd window of waku's own. Null, with a warning, when the
// host is not valid. There is no fallback that replaces Explorer's tray window.
std::unique_ptr<TrayHost> createTrayHost(const TrayHostFactories& factories);

}  // namespace waku::tray

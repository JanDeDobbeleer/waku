#pragma once

#include <memory>

#include "features/startup/startup_system.h"

namespace waku::startup {

// The Windows StartupSystem: Task Scheduler 2.0 COM API, SearchPathW and the Startup folder.
std::unique_ptr<StartupSystem> makeWin32StartupSystem();

}  // namespace waku::startup

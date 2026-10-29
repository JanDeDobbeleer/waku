#pragma once

#include <QString>
#include <QStringList>
#include <QUuid>
#include <optional>
#include <vector>

namespace wabar::control_center {

// --- power plans (services/power_plan/power_plan_api.py) -----------------------------------------

struct PowerPlan {
  QUuid guid;
  QString name;
};

// All installed schemes in enumeration order. `active` is the active scheme, if it can be read.
std::vector<PowerPlan> powerPlans(std::optional<QUuid>* active = nullptr);
// Activates `guid`; returns 0 (ERROR_SUCCESS) or the Win32 error code.
unsigned long setPowerPlan(const QUuid& guid);

// --- power mode overlays (services/power_mode/power_mode_api.py) ---------------------------------

struct PowerMode {
  QString name;
  QUuid guid;  // the Balanced overlay is the null GUID
};

std::vector<PowerMode> powerModes();
// Only Balanced-personality plans (personality value 2) support the Windows 11 overlays.
bool powerModeSupported();
std::optional<PowerMode> activePowerMode();
bool setPowerMode(const QUuid& guid);

// True on Windows 10 (build below 22000), where the mode overlays do not exist.
bool isWindows10();

// --- session and power actions (services/power_menu/power_commands.py) ---------------------------

void lockWorkstation();
// SetSuspendState(sleep) with the shutdown privilege; falls back to rundll32.
void suspendSystem();
// `shutdown /h`.
void hibernateSystem();
// Starts `shutdown <args>` hidden and detached.
bool runShutdown(const QStringList& arguments);

}  // namespace wabar::control_center

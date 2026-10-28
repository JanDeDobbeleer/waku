#pragma once

#include <QString>
#include <QUuid>
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

#include "core/config/options/control_center.h"
#include "platform/audio.h"
#include "platform/brightness.h"
#include "platform/media_session.h"
#include "platform/power.h"

namespace yasb::widgets {

class PollingWorkerRegistry;

// Everything the control center reads from or does to the system, behind replaceable seams so the
// widget tests never touch audio devices, monitors, the registry, power settings or processes.
// The service pointers are owned by the runtime's shared-service registry (or by the test); a null
// pointer disables the matching control, as a missing service does in Python.
struct ControlCenterSystem {
  platform::AudioEndpoints* output = nullptr;
  platform::AudioEndpoints* input = nullptr;
  platform::BrightnessControl* brightness = nullptr;
  platform::MediaSessionService* media = nullptr;

  // Focus Assist: "disabled", "priority", "alarms" or "unknown".
  std::function<QString()> dndStatus;
  std::function<void(const QString&)> setDndStatus;
  // "light" or "dark"; toggle returns the new mode, nullopt on failure.
  std::function<QString()> themeMode;
  std::function<std::optional<QString>()> toggleTheme;
  std::function<void()> toggleTouchKeyboard;
  // Interim screenshot action until the editor lands (U19): the system snipping overlay.
  std::function<void()> startScreenshot;
  // ShellExecute of a URI such as ms-settings:network, and of a configured quick-action command.
  std::function<void(const QString&)> openLink;
  std::function<void(const QString&, const QString&)> runCommand;
  // Plays the system beep after a volume drag.
  std::function<void()> beep;

  std::function<void()> lock;
  std::function<void()> signOut;
  std::function<void()> sleep;
  std::function<void()> hibernate;
  std::function<void()> restart;
  std::function<void()> shutdown;

  std::function<std::vector<platform::PowerPlan>(std::optional<QUuid>*)> powerPlans;
  std::function<unsigned long(const QUuid&)> setPowerPlan;
  std::function<std::vector<platform::PowerMode>()> powerModes;
  std::function<bool()> powerModeSupported;
  std::function<std::optional<platform::PowerMode>()> activePowerMode;
  std::function<bool(const QUuid&)> setPowerMode;
  bool windows10 = false;

  std::function<QString()> userName;
  std::function<QString()> avatarPath;
  // Native monitor handle of the monitor the widget sits on (0 when unknown).
  std::function<qulonglong(QWidget*)> monitorOf;
};

// The real system seams. Creates (or reuses, through the shared-service map) only the services the
// configured sections need, as the Python widget does.
ControlCenterSystem makeSystemControlCenter(PollingWorkerRegistry& registry,
                                            const config::options::ControlCenterConfig& options);

}  // namespace yasb::widgets

#pragma once

#include <QString>

namespace yasb::platform {

// Windows Focus Assist / Do Not Disturb (services/dnd/dnd_api.py DndService), through the
// undocumented IQuietHoursSettings COM object. Modes: "disabled", "priority", "alarms".

// "unknown" when the settings object cannot be created or read.
QString dndStatus();
// False for an unknown mode or when the write fails.
bool setDndStatus(const QString& mode);

}  // namespace yasb::platform

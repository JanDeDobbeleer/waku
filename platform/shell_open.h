#pragma once

#include <QString>

namespace yasb::platform {

// utils/shell_utils.shell_open: ShellExecuteW ("open") on a detached thread so a slow launch never
// blocks the GUI thread. Returns at once; failures are logged.
void shellOpen(const QString& file, const QString& parameters = QString());

}  // namespace yasb::platform

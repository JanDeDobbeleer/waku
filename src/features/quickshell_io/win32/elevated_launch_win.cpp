#include <QDebug>

#include "features/quickshell_io/command_spec.h"
#include "kernel/win32/shell_launch.h"

namespace waku::quickshell_io {

// The "runas" verb raises the UAC prompt; the child is not a child of ours and cannot be piped.
// kernel::shellRunElevated waits for the prompt on its own thread, so this returns at once and true
// means the request was handed over.
bool startElevated(const CommandSpec& spec) {
  if (spec.command.isEmpty()) return false;
  if (!spec.environment.isEmpty() || spec.clearEnvironment) {
    qWarning() << "elevated launch ignores the environment of" << spec.command.first();
  }
  kernel::shellRunElevated(spec.command.first(), windowsArguments(spec.command.mid(1)),
                           spec.workingDirectory);
  return true;
}

}  // namespace waku::quickshell_io

#include "host/qml/windows_taskbar_command.h"

#include "host/windows_taskbar.h"

namespace wabar::host {

QString answerWindowsTaskbarCommand(const QStringList& words, WindowsTaskbar* taskbar) {
  static const QString usage = QStringLiteral("error: usage: taskbar hide | show | status");
  if (words.size() != 1) return usage;
  const QString verb = words.first();
  if (verb != QLatin1String("hide") && verb != QLatin1String("show") &&
      verb != QLatin1String("status")) {
    return QStringLiteral("error: unknown taskbar command '%1' (try: hide, show, status)")
        .arg(verb.left(40));
  }
  if (!taskbar) return QStringLiteral("error: the taskbar is not available");
  if (verb == QLatin1String("hide")) taskbar->hide();
  if (verb == QLatin1String("show")) taskbar->restore();
  return QStringLiteral("ok: taskbar: ") + taskbarStateName(taskbar->state());
}

}  // namespace wabar::host

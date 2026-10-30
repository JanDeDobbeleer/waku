#pragma once

#include <QString>
#include <QStringList>

namespace wabar::host {

class TaskbarControl;

// The `taskbar` commands of the control channel (the words after "taskbar"):
//   hide     "ok: taskbar: hidden": turns auto-hide on and hides the taskbar windows
//   show     "ok: taskbar: <state>": restores what wabar changed; the taskbar stays shown until
//            `hide`
//   status   "ok: taskbar: hidden|visible|autohide-was-on" (what is seen, whoever hid it)
// Errors: "error: usage: ...", "error: unknown taskbar command '...'", "error: the taskbar is not
// available" (no control: the offscreen platform). Public for tests.
QString answerTaskbarCommand(const QStringList& words, TaskbarControl* taskbar);

}  // namespace wabar::host

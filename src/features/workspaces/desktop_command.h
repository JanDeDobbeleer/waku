#pragma once

#include <QString>
#include <QStringList>

namespace waku::workspaces {

// What the `desktop` commands of `wakuc` ask of the Windows virtual desktops. The parser below
// depends on this interface only (so its tests use a fake); the VirtualDesktops service
// implements it. Every function returns the answer line: "ok: <text>" or "error: <text>".
class DesktopControl {
 public:
  virtual ~DesktopControl() = default;
  // Shows desktop `number` (1-based). Never creates a desktop.
  virtual QString controlGoto(int number) = 0;
  // Sends the focused window to desktop `number` (1-based). Never creates a desktop and does not
  // follow the window.
  virtual QString controlMove(int number) = 0;
  // Shows the next (+1) or previous (-1) desktop; no wrap at either end.
  virtual QString controlStep(int direction) = 0;
  virtual QString controlStatus() = 0;
};

// The `desktop` command of the control pipe, given the words after "desktop" (split as
// control::splitCommand does):
//   goto <n> | move <n> | next | previous | status
// `n` counts from 1. Bad usage answers "error: usage: ..." and calls nothing; a null `desktops`
// answers "error: virtual desktops are not available". The composition root registers it with the
// bar's control handler (host::ControlCommands), so neither `control` nor `host` knows the verb.
QString answerDesktopCommand(const QStringList& words, DesktopControl* desktops);

}  // namespace waku::workspaces

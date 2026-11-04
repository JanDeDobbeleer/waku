#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace waku::tiling {

// What the `tile` commands of `wakuc` ask of the tiler. The parser below depends on this
// interface only (so its tests use a fake); the Tiling service implements it. Every function
// returns the answer line: "ok: <text>" or "error: <text>".
class TilingControl {
 public:
  virtual ~TilingControl() = default;
  // Floats or tiles the focused window; `floating` unset toggles.
  virtual QString controlFloat(std::optional<bool> floating) = 0;
  // Sets the layout of the focused window's workspace.
  virtual QString controlLayout(const QString& name) = 0;
  virtual QString controlRetile() = 0;
  // Focuses the next (+1) or previous (-1) window of the workspace.
  virtual QString controlFocus(int direction) = 0;
  virtual QString controlStatus() = 0;
};

// The `tile` command of the control pipe, given the words after "tile" (split as
// control::splitCommand does):
//   toggle | float | layout <name> | retile | focus next|previous | status
// `toggle` floats a tiled window and tiles a floating one; `float` only floats. Bad usage answers
// "error: usage: ..." and calls nothing; a null `tiling` answers "error: the tiler is not
// available". The composition root registers it with the bar's control handler
// (host::ControlCommands), so neither `control` nor `host` knows the verb.
QString answerTileCommand(const QStringList& words, TilingControl* tiling);

}  // namespace waku::tiling

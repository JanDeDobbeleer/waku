#pragma once

#include <QString>
#include <QStringList>
#include <optional>

#include "features/tiling/layouts.h"

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
  // Activates the nearest tiled window in `direction`.
  virtual QString controlFocusDirection(Direction direction) = 0;
  // Swaps the focused window with its neighbour in `direction`.
  virtual QString controlMove(Direction direction) = 0;
  // Gives the focused window the main slot.
  virtual QString controlPromote() = 0;
  // Grows (`sign` > 0) or shrinks the focused window along `axis`.
  virtual QString controlResize(Axis axis, int sign) = 0;
  // Puts the focused window on the stack of the slot nearest in `direction`.
  virtual QString controlStack(Direction direction) = 0;
  // Takes the focused window out of its stack into a slot of its own.
  virtual QString controlUnstack() = 0;
  // Brings the next (+1) or previous (-1) window of the focused window's stack to the top.
  virtual QString controlCycleStack(int step) = 0;
  // Mirrors the layout of the focused window's workspace.
  virtual QString controlFlip(Axis axis) = 0;
  // The focused window fills the work area, or goes back to its slot.
  virtual QString controlMonocle() = 0;
  // Stops and resumes placing windows.
  virtual QString controlPause() = 0;
  // Politely closes or minimizes the focused window.
  virtual QString controlClose() = 0;
  virtual QString controlMinimize() = 0;
};

// The `tile` command of the control pipe, given the words after "tile" (split as
// control::splitCommand does):
//   toggle | float | layout <name> | retile | status
//   focus next|previous|left|right|up|down     (left... : the nearest tiled window that way)
//   move left|right|up|down                    swap with the neighbour in that direction
//   promote                                    the focused window takes the main slot
//   stack left|right|up|down                   put the focused window on the stack of the slot
//   there unstack                                    the focused window leaves its stack for its
//   own slot cycle-stack next|previous                  bring the next or previous stacked window
//   to the top resize horizontal|vertical increase|decrease flip horizontal|vertical mirror the
//   layout; again undoes it monocle                                    the focused window fills the
//   work area; again undoes pause                                      stop and resume placing
//   windows close | minimize                           the focused window
// `toggle` floats a tiled window and tiles a floating one; `float` only floats. Bad usage answers
// "error: usage: ..." and calls nothing; a null `tiling` answers "error: the tiler is not
// available". The composition root registers it with the bar's control handler
// (host::ControlCommands), so neither `control` nor `host` knows the verb.
QString answerTileCommand(const QStringList& words, TilingControl* tiling);

}  // namespace waku::tiling

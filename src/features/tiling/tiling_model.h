#pragma once

#include <QString>
#include <compare>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "features/tiling/window_placer.h"

namespace waku::tiling {

// One workspace of the tiler: the windows of one monitor on one virtual desktop. Keyed by the
// monitor and the desktop GUID, not by window order, so a desktop switch loses nothing.
struct WorkspaceKey {
  MonitorId monitor = 0;
  QString desktop;

  friend bool operator==(const WorkspaceKey&, const WorkspaceKey&) = default;
  friend bool operator<(const WorkspaceKey& a, const WorkspaceKey& b) {
    if (a.monitor != b.monitor) return a.monitor < b.monitor;
    return a.desktop < b.desktop;
  }
};

// The per-workspace adjustments the `tile` commands make, on top of the layout. A workspace with
// none has the defaults.
struct WorkspaceView {
  std::optional<double> ratio;         // overrides the default main share of mainStack
  bool flipHorizontal = false;         // mirror left to right
  bool flipVertical = false;           // mirror top to bottom
  WindowId monocle = 0;                // the window that fills the work area; 0 for none
  std::map<WindowId, double> weights;  // height weights of the stacked windows (default 1)
};

// One slot of a layout. Usually one window; windows that were stacked onto it share it, and only
// the top one shows (the others sit under it at the same place).
struct Slot {
  std::vector<WindowId> windows;  // in stacking order; never empty
  size_t top = 0;                 // index of the visible window

  WindowId topWindow() const { return windows[top]; }
};

// What the tiler remembers: per workspace an ordered list of slots (the first is the main one;
// each holds one window or a stack of windows), a layout name that overrides the default, and one
// set of floating windows (never placed). In memory only. GUI thread only.
//
// "The stack" below is the list of slots, in order, as their top windows: the layout counts slots,
// not windows. A window that is below the top of a stack is still managed.
class TilingModel {
 public:
  // Appends `hwnd` to the stack of `key`; moves it there when it is in another stack. A window
  // that floats leaves the float set.
  void add(const WorkspaceKey& key, WindowId hwnd);
  // Takes the window out of its stack; the workspace it left, or nothing when it was in none.
  std::optional<WorkspaceKey> remove(WindowId hwnd);

  bool contains(WindowId hwnd) const { return owner_.count(hwnd) != 0; }
  std::optional<WorkspaceKey> keyOf(WindowId hwnd) const;
  // The top window of every slot of `key`, in order; empty for an unknown workspace.
  std::vector<WindowId> stack(const WorkspaceKey& key) const;
  // The slots of `key` in order; empty for an unknown workspace.
  const std::vector<Slot>& slotList(const WorkspaceKey& key) const;
  // Every window of `key`, hidden ones included.
  std::vector<WindowId> members(const WorkspaceKey& key) const;
  // The windows that share a slot with `hwnd` (itself included); empty when it is not managed.
  std::vector<WindowId> stackOf(WindowId hwnd) const;
  // The number of slots that hold more than one window.
  int stackCount() const;
  // True when `hwnd` is the visible window of its slot.
  bool isTop(WindowId hwnd) const;
  // The workspaces that have at least one window.
  std::vector<WorkspaceKey> keys() const;
  int managedCount() const { return static_cast<int>(owner_.size()); }

  void setFloating(WindowId hwnd, bool floating);
  bool isFloating(WindowId hwnd) const { return floating_.count(hwnd) != 0; }
  int floatingCount() const { return static_cast<int>(floating_.size()); }
  // Forgets the window entirely (stack and float set).
  void forget(WindowId hwnd);

  // The layout of a workspace: its own when set, else `fallback`.
  QString layout(const WorkspaceKey& key, const QString& fallback) const;
  void setLayout(const WorkspaceKey& key, const QString& name);
  void clearLayouts() { layouts_.clear(); }

  // The adjustments of a workspace (the defaults for an unknown one) and a way to change them.
  const WorkspaceView& view(const WorkspaceKey& key) const;
  WorkspaceView& edit(const WorkspaceKey& key) { return views_[key]; }
  // Forgets every ratio override (the default ratio changed).
  void clearRatios();

  // Exchanges the slots of two windows of one workspace (a stack moves as a whole); false when
  // they are not in the same workspace or share a slot.
  bool swap(WindowId a, WindowId b);
  // Moves the slot of the window to the front (the main slot); false when it is there already or
  // the window is in no workspace.
  bool promote(WindowId hwnd);

  // Moves `hwnd` onto the slot of `target` (another window of the same workspace) and makes it the
  // top there. The slot it leaves is dropped when it was alone, else its next window shows. False
  // when either is unmanaged, they are in different workspaces or already share a slot.
  bool stackOnto(WindowId hwnd, WindowId target);
  // Takes `hwnd` out of its stack into a slot of its own right after the stack's slot. False when
  // it is alone in its slot.
  bool unstack(WindowId hwnd);
  // Makes the next (`step` > 0) or previous window of the stack of `hwnd` the top, wrapping;
  // the new top, or 0 when `hwnd` is not in a stack.
  WindowId cycle(WindowId hwnd, int step);
  // Makes `hwnd` the top of its slot; false when it is not managed or is the top already.
  bool raiseInStack(WindowId hwnd);

  void clear();

 private:
  struct Where {
    std::vector<Slot>* list = nullptr;
    size_t slot = 0;
    size_t index = 0;
  };
  Where find(WindowId hwnd);
  // Makes the window at `index` of the slot the top; the old top hands its height weight and the
  // monocle mark over.
  void setTop(const WorkspaceKey& key, Slot& slot, size_t index);

  std::map<WorkspaceKey, std::vector<Slot>> stacks_;
  std::map<WindowId, WorkspaceKey> owner_;
  std::map<WorkspaceKey, QString> layouts_;
  std::map<WorkspaceKey, WorkspaceView> views_;
  std::set<WindowId> floating_;
};

}  // namespace waku::tiling

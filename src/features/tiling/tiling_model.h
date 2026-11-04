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

// What the tiler remembers: per workspace an ordered stack (the first window is the main one), a
// layout name that overrides the default, and one set of floating windows (never placed). In
// memory only. GUI thread only.
class TilingModel {
 public:
  // Appends `hwnd` to the stack of `key`; moves it there when it is in another stack. A window
  // that floats leaves the float set.
  void add(const WorkspaceKey& key, WindowId hwnd);
  // Takes the window out of its stack; the workspace it left, or nothing when it was in none.
  std::optional<WorkspaceKey> remove(WindowId hwnd);

  bool contains(WindowId hwnd) const { return owner_.count(hwnd) != 0; }
  std::optional<WorkspaceKey> keyOf(WindowId hwnd) const;
  // The stack of `key` in order; empty for an unknown workspace.
  const std::vector<WindowId>& stack(const WorkspaceKey& key) const;
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

  // Exchanges the places of two windows of one stack; false when they are not in the same one.
  bool swap(WindowId a, WindowId b);
  // Moves the window to the front of its stack (the main slot); false when it is there already or
  // is in no stack.
  bool promote(WindowId hwnd);

  void clear();

 private:
  std::map<WorkspaceKey, std::vector<WindowId>> stacks_;
  std::map<WindowId, WorkspaceKey> owner_;
  std::map<WorkspaceKey, QString> layouts_;
  std::map<WorkspaceKey, WorkspaceView> views_;
  std::set<WindowId> floating_;
};

}  // namespace waku::tiling

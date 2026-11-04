#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace waku::workspaces::vdesk {

// One Windows virtual desktop.
struct Desktop {
  QString id;    // the desktop GUID in registry form: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
  QString name;  // what the user named it; empty when it has the default name
  // HWNDs of the windows on this desktop, ascending (a stable order, so an unchanged desktop
  // compares equal). The count is windows.size().
  std::vector<quint64> windows;

  friend bool operator==(const Desktop&, const Desktop&) = default;
};

// The desktops in Task View order and the index of the one shown.
struct State {
  std::vector<Desktop> desktops;
  int current = -1;

  friend bool operator==(const State&, const State&) = default;
};

// Splits the registry value `VirtualDesktopIDs` (or the 16 bytes of `CurrentVirtualDesktop`) into
// GUID strings. A trailing partial entry is dropped.
QStringList parseDesktopIds(const QByteArray& bytes);

// The index of the desktop that receives the windows of the desktop at `index` when it is closed,
// among `count` desktops, or -1 when it cannot be closed. Task View's rule: the left neighbour; the
// first desktop gives its windows to the right one; the last remaining desktop is never closed.
int removalFallbackIndex(int count, int index);

// Platform access. Replaceable so tests never talk to the live desktop.
class VirtualDesktopSystem {
 public:
  virtual ~VirtualDesktopSystem() = default;
  // The desktops now, or nothing when Windows does not expose them (the registry keys are gone,
  // as on a Windows build without virtual desktops).
  virtual std::optional<State> read() = 0;
  // Shows the desktop at `index`. The real system asks the shell (the same call Win+Ctrl+Arrow
  // makes, animated unless setAnimate(false)) and falls back to a window of this process that is
  // moved to that desktop and activated (documented IVirtualDesktopManager::MoveWindowToDesktop).
  // False when neither works. The change shows up in the next read().
  virtual bool jumpTo(int index) = 0;
  // The outcome of moveForegroundWindow().
  enum class MoveResult {
    Moved,
    NoWindow,     // nothing is in front, or only the desktop or the taskbar
    OwnWindow,    // a window of waku itself: not moved
    Refused,      // Windows refused, or the desktop does not exist
    Unsupported,  // this system cannot move windows
  };
  // Moves the top-level window `hwnd` to the desktop at `index` without showing that desktop.
  // The real system asks the shell (the move Task View's "Move to" makes, which works for windows
  // of any process) and falls back to the documented IVirtualDesktopManager::MoveWindowToDesktop
  // (which Windows refuses for windows of other processes). Creates nothing. Refused when the
  // window or the desktop does not exist, or when both routes fail.
  virtual MoveResult moveWindow(quint64 hwnd, int index) {
    Q_UNUSED(hwnd);
    Q_UNUSED(index);
    return MoveResult::Unsupported;
  }
  // Moves the foreground window to the desktop at `index` without showing that desktop, by way of
  // moveWindow(). Creates nothing. If `index` is not the shown desktop the window leaves this one.
  virtual MoveResult moveForegroundWindow(int index) {
    Q_UNUSED(index);
    return MoveResult::Unsupported;
  }
  // Creates one desktop. Returns its index when the system made it without showing it (the caller
  // shows it with jumpTo), or -1 when Windows showed the new desktop itself (the Win+Ctrl+D
  // fallback, which has no documented call) or nothing was made.
  virtual int create() = 0;
  // True when remove() can work: the shell interface is open. Without it there is no way to close
  // a desktop that does not switch to it and back (a keystroke would show).
  virtual bool canRemove() const { return false; }
  // Closes the desktop at `index` like Task View's close button: its windows go to
  // removalFallbackIndex(). False when refused or not supported. No fallback route.
  virtual bool remove(int index) {
    Q_UNUSED(index);
    return false;
  }
  // Names the desktop at `index` (an empty name restores the default). False when not supported.
  virtual bool rename(int index, const QString& name) {
    Q_UNUSED(index);
    Q_UNUSED(name);
    return false;
  }
  // Whether jumpTo() animates like Task View (default) or switches at once.
  virtual void setAnimate(bool animate) { Q_UNUSED(animate); }
  // Installs the hook the system calls, on the GUI thread, when the desktops may have changed
  // (a shell notification or a registry change). The hook only schedules a read. Returns true when
  // at least one source is active; then no 500 ms poll is needed.
  virtual bool setChangedHook(std::function<void()> hook) {
    Q_UNUSED(hook);
    return false;
  }
};

// The real system (win32/virtual_desktops_win.cpp).
std::unique_ptr<VirtualDesktopSystem> makeWin32VirtualDesktopSystem();

// The GUID of the shown desktop (registry form, upper case), from the one registry value
// `CurrentVirtualDesktop`; nothing when Windows does not expose it. Cheap: no window is
// enumerated. Defined in win32/virtual_desktops_win.cpp; the tiler asks it after a window event.
std::optional<QString> currentDesktopId();

}  // namespace waku::workspaces::vdesk

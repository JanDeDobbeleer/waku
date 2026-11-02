#pragma once

#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace waku::host {

// Hiding the Windows taskbar while waku runs (`waku --hide-taskbar`, `wakuc taskbar ...`).
//
// What is seen from outside. `AutoHide` is the answer "autohide-was-on": the taskbar window is
// not hidden but Windows auto-hides it.
enum class TaskbarState { Visible, Hidden, AutoHide };

// "visible", "hidden" or "autohide-was-on".
QString taskbarStateName(TaskbarState state);

// What the control verbs and main.cpp use.
class WindowsTaskbar {
 public:
  virtual ~WindowsTaskbar() = default;
  // Turns auto-hide on (releases the reserved work area) and hides the taskbar windows. Does
  // nothing when waku already hides the taskbar.
  virtual void hide() = 0;
  // Undoes exactly what hide() changed. Does nothing when waku does not hide the taskbar.
  virtual void restore() = 0;
  // What is seen now, whoever caused it.
  virtual TaskbarState state() const = 0;
};

// The operating system under the logic. The Windows one is makeWin32TaskbarSystem(); tests use the
// fake of host/testing/windows_taskbar_fakes.h. GUI thread only.
class TaskbarSystem {
 public:
  virtual ~TaskbarSystem() = default;
  // ABM_GETSTATE: is ABS_AUTOHIDE set?
  virtual bool autoHide() const = 0;
  // ABM_SETSTATE, keeping the other state bits.
  virtual void setAutoHide(bool on) = 0;
  // True when at least one taskbar window (Shell_TrayWnd, Shell_SecondaryTrayWnd) is visible.
  virtual bool windowsVisible() const = 0;
  // SW_HIDE or SW_SHOWNA on every taskbar window.
  virtual void setWindowsHidden(bool hidden) = 0;
  // True when `pid` is a running waku process.
  virtual bool processAlive(qint64 pid) const = 0;
  // `callback` runs when Explorer has recreated the taskbar (the TaskbarCreated message).
  virtual void onExplorerRestart(std::function<void()> callback) = 0;
};

// The Windows implementation (win32/windows_taskbar_win.cpp). Creates a hidden window for the
// TaskbarCreated broadcast, so it needs the GUI thread's message loop.
std::unique_ptr<TaskbarSystem> makeWin32TaskbarSystem();

// What waku changed, written to disk before the first change so a crashed run can be undone.
struct TaskbarMarker {
  qint64 pid = 0;
  bool changedAutoHide = false;  // waku turned auto-hide on (it was off)
  bool changedWindows = false;   // waku hid the windows (they were visible)
  bool operator==(const TaskbarMarker&) const = default;
};

class TaskbarMarkerStore {
 public:
  virtual ~TaskbarMarkerStore() = default;
  virtual std::optional<TaskbarMarker> read() const = 0;
  virtual void write(const TaskbarMarker& marker) = 0;
  virtual void remove() = 0;
};

// The marker as a file (one small text file, `taskbar_hidden.marker` in appDataDir()).
std::unique_ptr<TaskbarMarkerStore> makeFileMarkerStore(const QString& path);
// The text of a marker and its parse (an empty optional for text that is not a marker). The
// file holds "previous_autohide=0|1" (0: waku turned it on) and "windows_were_visible=0|1".
// Public for tests.
QString formatTaskbarMarker(const TaskbarMarker& marker);
std::optional<TaskbarMarker> parseTaskbarMarker(const QString& text);

// The logic. Co-existence: when another tool already hides the taskbar (thide), hide() changes
// only what is still unchanged and restore() undoes only that; state() reports what is seen.
class WindowsTaskbarHider : public WindowsTaskbar {
 public:
  // `ownPid`: this process. `system` and `store` outlive the object.
  WindowsTaskbarHider(TaskbarSystem& system, TaskbarMarkerStore& store, qint64 ownPid);
  // Restores what this object hid.
  ~WindowsTaskbarHider() override;

  // The start of a run: undoes a stale marker (recoverStale()), then hides when `hideNow` is set.
  // With `hideNow` false the taskbar is not touched, except to recover.
  void start(bool hideNow);
  // A marker left by a run that is not alive any more: undoes its changes and deletes it. A
  // marker whose pid is another live waku is left alone. Returns true when it undid something.
  bool recoverStale();

  void hide() override;
  void restore() override;
  TaskbarState state() const override;

  bool hiding() const { return hiding_; }

 private:
  void reassert();

  TaskbarSystem& system_;
  TaskbarMarkerStore& store_;
  qint64 ownPid_;
  bool hiding_ = false;
  bool changedAutoHide_ = false;
  bool changedWindows_ = false;
};

}  // namespace waku::host

#pragma once

#include <QObject>
#include <QtGlobal>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "features/taskbar/window_system.h"

namespace waku::taskbar {

// The taskbar progress every window last reported (ITaskbarList3::SetProgressState and
// SetProgressValue), kept for the whole run so a shell reload, a hidden window or a window the
// tracker adds late still shows what its application said before. An entry is keyed by the window
// and the process that owned it when it reported: a reused window handle never inherits the state.
//
// Threading: any thread may call it (the receiver writes from its worker thread, the tracker reads
// on the GUI thread); one mutex guards the map. `changed` is emitted on the writing thread, so a
// GUI-thread receiver gets it queued.
class ProgressCache : public QObject {
  Q_OBJECT

 public:
  explicit ProgressCache(QObject* parent = nullptr) : QObject(parent) {}

  // `state` is a TBPFLAG (0 none, 1 indeterminate, 2 normal, 4 error, 8 paused); anything else is
  // ignored. None forgets the window.
  void setState(WindowHandle hwnd, std::uint32_t pid, int state);
  // `value` is a fraction from 0 to 1. As in ITaskbarList3, a value turns none and indeterminate
  // into normal; error and paused keep their state.
  void setValue(WindowHandle hwnd, std::uint32_t pid, double value);

  // The progress `hwnd` reported while `pid` owned it; nothing for another owner.
  std::optional<WindowProgress> get(WindowHandle hwnd, std::uint32_t pid) const;
  void forget(WindowHandle hwnd);
  // Forgets every window for which `alive(hwnd, pid)` is false.
  void prune(const std::function<bool(WindowHandle, std::uint32_t)>& alive);
  std::size_t size() const;

 signals:
  // The progress of `hwnd` changed or was forgotten. Read it with get().
  void changed(quintptr hwnd);

 private:
  struct Entry {
    std::uint32_t pid = 0;
    WindowProgress progress;
  };

  // Applies `update` to the entry of `hwnd` (a fresh one for a new owner) and reports a change.
  void apply(WindowHandle hwnd, std::uint32_t pid,
             const std::function<void(WindowProgress&)>& update);

  mutable std::mutex mutex_;
  std::map<WindowHandle, Entry> entries_;
};

// Receives what applications send to the taskbar (see design.md, "Progress") and writes it to a
// cache. It runs until it is destroyed.
class ProgressReceiver {
 public:
  virtual ~ProgressReceiver() = default;
};

// Null when the receiver window cannot be created.
std::unique_ptr<ProgressReceiver> makeWin32ProgressReceiver(std::shared_ptr<ProgressCache> cache);

}  // namespace waku::taskbar

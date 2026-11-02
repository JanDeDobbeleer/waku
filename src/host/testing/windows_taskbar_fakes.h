#pragma once

// Fakes for the taskbar seams (host/windows_taskbar.h). Header-only; tests only. They never touch
// the real taskbar.

#include <QSet>
#include <QStringList>
#include <functional>
#include <optional>

#include "host/windows_taskbar.h"

namespace waku::host {

class FakeTaskbarSystem : public TaskbarSystem {
 public:
  bool autoHide() const override { return autoHide_; }
  void setAutoHide(bool on) override {
    autoHide_ = on;
    calls << (on ? QStringLiteral("autohide-on") : QStringLiteral("autohide-off"));
  }
  bool windowsVisible() const override { return windowsVisible_; }
  void setWindowsHidden(bool hidden) override {
    windowsVisible_ = !hidden;
    calls << (hidden ? QStringLiteral("hide-windows") : QStringLiteral("show-windows"));
  }
  bool processAlive(qint64 pid) const override { return alive.contains(pid); }
  void onExplorerRestart(std::function<void()> callback) override {
    restartCallback = std::move(callback);
  }

  // What Explorer does when it restarts: new windows, visible.
  void restartExplorer() {
    windowsVisible_ = true;
    if (restartCallback) restartCallback();
  }

  bool autoHide_ = false;
  bool windowsVisible_ = true;
  QSet<qint64> alive;
  QStringList calls;  // the mutating calls, in order
  std::function<void()> restartCallback;
};

class FakeMarkerStore : public TaskbarMarkerStore {
 public:
  std::optional<TaskbarMarker> read() const override { return marker; }
  void write(const TaskbarMarker& m) override {
    marker = m;
    ++writes;
  }
  void remove() override { marker.reset(); }

  std::optional<TaskbarMarker> marker;
  int writes = 0;
};

// A WindowsTaskbar that only counts calls (for the control verbs).
class FakeWindowsTaskbar : public WindowsTaskbar {
 public:
  void hide() override {
    ++hides;
    state_ = TaskbarState::Hidden;
  }
  void restore() override {
    ++restores;
    state_ = TaskbarState::Visible;
  }
  TaskbarState state() const override { return state_; }

  int hides = 0;
  int restores = 0;
  TaskbarState state_ = TaskbarState::Visible;
};

}  // namespace waku::host

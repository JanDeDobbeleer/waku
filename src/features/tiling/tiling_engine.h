#pragma once

#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "features/tiling/tiling_events.h"
#include "features/tiling/tiling_model.h"
#include "features/tiling/tiling_rules.h"
#include "features/tiling/window_placer.h"

namespace waku::tiling {

// Runs a task later on the calling thread. The real one is a QTimer::singleShot; tests use a
// manual clock (testing/fake_window_placer.h).
class Scheduler {
 public:
  virtual ~Scheduler() = default;
  virtual void after(int ms, std::function<void()> task) = 0;
};

std::unique_ptr<Scheduler> makeQtScheduler();

struct TilingSettings {
  QString layout = QStringLiteral("mainStack");  // the layout of a workspace with none of its own
  int gap = 8;                                   // pixels between windows and around them
  double ratio = 0.55;                           // the share of the width the main window takes
  std::vector<Rule> rules;
  bool selfElevated = false;  // waku runs elevated: elevated windows are movable
};

struct TilingStatus {
  bool running = false;
  QString desktop;
  int managed = 0;
  int floating = 0;
  int workspaces = 0;
  int clamped = 0;  // placed windows whose bounds differ from their slot (a minimum size)
  QString layout;
};

enum class FloatOutcome { NoWindow, Floated, Tiled, Unchanged, Ineligible };

// The tiler: takes window events and keeps every managed window of the shown virtual desktop in
// the slot its layout gives it. Everything it knows about a window comes from the WindowPlacer, so
// the whole behaviour runs against a fake in tests. No thread of its own: the caller feeds it
// events on the GUI thread and the scheduler runs its deferred work on the same thread.
//
// What it does, per event (the hooks send each as it happens):
//  - a window that shows, uncloaks, restores or gets a title is evaluated one event-loop turn
//    later (Show arrives before the final title and size); a window that is still minimized or
//    untitled is looked at again every 100 ms, five times at most
//  - a window that goes away, hides or minimizes leaves its stack
//  - a window that the user moved or resized (MoveSizeEnd with another rectangle) floats
//  - a desktop switch has no event: the first Uncloaked or Foreground after it finds a different
//    current desktop and retiles that desktop
//  - retiles are coalesced: any number of requests in one turn place the windows once
// It never activates a window, never places a cloaked, minimized or maximized one, and reads the
// rectangle back after a placement: a window that clamps (minimum size) keeps its slot and leaves
// a gap; there is no second pass.
class TilingEngine {
 public:
  static constexpr int kRetryMs = 100;
  static constexpr int kMaxRetries = 5;
  // How long after a placement the engine reads the window's bounds back: SetWindowPos is posted,
  // so the window applies it a moment later.
  static constexpr int kVerifyMs = 150;

  TilingEngine(WindowPlacer& placer, Scheduler& scheduler);
  ~TilingEngine();
  TilingEngine(const TilingEngine&) = delete;
  TilingEngine& operator=(const TilingEngine&) = delete;

  void setSettings(const TilingSettings& settings);
  const TilingSettings& settings() const { return settings_; }

  // Scans the open windows and tiles them. Idempotent.
  void start();
  // Forgets everything and leaves the windows where they are.
  void stop();
  bool running() const { return running_; }

  void handle(const WindowEvent& event);

  // Retiles every workspace of the shown desktop (a screen or work area change).
  void retileAll();

  // Floats or tiles the foreground window: `floating` unset toggles.
  FloatOutcome setForegroundFloating(std::optional<bool> floating);
  // The layout of the workspace of the foreground window (the default layout when no managed
  // window is in front). False for an unknown name.
  bool setWorkspaceLayout(const QString& name);
  // Activates the next or previous window of the foreground window's workspace, wrapping.
  bool focusStep(int direction);

  TilingStatus status() const;
  const TilingModel& model() const { return model_; }

 private:
  void defer(int ms, std::function<void()> task);
  void scheduleEvaluate(WindowId hwnd, int ms, int attempt, bool force);
  void evaluate(WindowId hwnd, int attempt, bool force);
  void forget(WindowId hwnd, bool dropFloat);
  void refreshDesktop();
  void requestRetile(const WorkspaceKey& key);
  void flushRetiles();
  void retile(const WorkspaceKey& key);
  void onCloaked(WindowId hwnd);
  void onMoveSizeStart(WindowId hwnd);
  void onMoveSizeEnd(WindowId hwnd);
  void verifyPlacements(const std::vector<std::pair<WindowId, QRect>>& placed);
  WorkspaceKey keyFor(const WindowFacts& facts) const;
  std::optional<WorkspaceKey> foregroundKey() const;

  WindowPlacer& placer_;
  Scheduler& scheduler_;
  TilingSettings settings_;
  TilingModel model_;
  bool running_ = false;
  QString currentDesktop_;
  // Windows that were looked at and rejected; the value is true when the reason was a missing
  // title, so a title change looks again.
  std::map<WindowId, bool> ignored_;
  std::set<WindowId> pending_;        // an evaluation is scheduled
  std::set<WindowId> moving_;         // the user drags or resizes it
  std::map<WindowId, QRect> placed_;  // the visible bounds the window had when we last looked
  std::set<WindowId> clamped_;        // placed, but not at the size of its slot
  std::set<WorkspaceKey> retiles_;
  bool flushScheduled_ = false;
  // Tasks hold a weak copy: a task that outlives stop() or the engine does nothing.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace waku::tiling

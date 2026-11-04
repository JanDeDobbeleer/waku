#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "features/tiling/tiling_engine.h"
#include "features/tiling/tiling_events.h"
#include "features/tiling/window_placer.h"

// Fakes for the tiler's tests. Nothing here touches a real window.
namespace waku::tiling::testing {

// A manual clock: tasks run when advance() reaches their time, in time order and then in the order
// they were queued. runAll() runs everything that is queued, whatever its delay.
class FakeScheduler final : public Scheduler {
 public:
  void after(int ms, std::function<void()> task) override {
    queue_.push_back({now_ + ms, seq_++, std::move(task)});
  }

  void advance(int ms) {
    const long long until = now_ + ms;
    runDue(until);
    now_ = until;
  }
  void runAll() {
    int guard = 0;
    while (!queue_.empty() && guard++ < 10000) {
      long long first = queue_.front().due;
      for (const Item& item : queue_) first = std::min(first, item.due);
      now_ = std::max(now_, first);
      runDue(now_);
    }
  }
  size_t pending() const { return queue_.size(); }
  long long now() const { return now_; }

 private:
  struct Item {
    long long due;
    long long seq;
    std::function<void()> task;
  };

  void runDue(long long until) {
    for (;;) {
      auto next = queue_.end();
      for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->due > until) continue;
        if (next == queue_.end() || it->due < next->due ||
            (it->due == next->due && it->seq < next->seq)) {
          next = it;
        }
      }
      if (next == queue_.end()) return;
      auto task = std::move(next->task);
      now_ = std::max(now_, next->due);
      queue_.erase(next);
      task();
    }
  }

  std::vector<Item> queue_;
  long long now_ = 0;
  long long seq_ = 0;
};

constexpr MonitorId kMonitorA = 0xA0;
constexpr MonitorId kMonitorB = 0xB0;

class FakeWindowPlacer final : public WindowPlacer {
 public:
  struct Placement {
    WindowId hwnd = 0;
    QRect slot;
  };

  FakeWindowPlacer() {
    areas[kMonitorA] = QRect(0, 0, 1920, 1040);
    areas[kMonitorB] = QRect(1920, 0, 1280, 1000);
    desktop = QStringLiteral("{D1}");
  }

  // A tileable window of `process` on monitor A and the shown desktop, floating at a random spot.
  WindowFacts& add(WindowId hwnd, const QString& process = QStringLiteral("app.exe"),
                   const QString& title = QStringLiteral("window")) {
    WindowFacts facts;
    facts.hwnd = hwnd;
    facts.visible = true;
    facts.thickFrame = true;
    facts.caption = true;
    facts.processName = process;
    facts.className = QStringLiteral("AppWindow");
    facts.title = title;
    facts.monitor = kMonitorA;
    facts.rect = QRect(100, 100, 640, 480);
    facts.desktopId = desktop;
    return windows[hwnd] = facts;
  }

  std::vector<WindowId> candidates() override {
    std::vector<WindowId> out;
    for (const auto& entry : windows) out.push_back(entry.first);
    return out;
  }
  std::optional<WindowFacts> query(WindowId hwnd) override {
    ++queries;
    const auto found = windows.find(hwnd);
    if (found == windows.end()) return std::nullopt;
    return found->second;
  }
  QRect workArea(MonitorId monitor) override {
    const auto found = areas.find(monitor);
    return found == areas.end() ? QRect() : found->second;
  }
  QString currentDesktop() override { return desktop; }
  std::optional<WindowId> foreground() override { return front; }

  PlaceResult place(WindowId hwnd, const QRect& slot) override {
    placements.push_back({hwnd, slot});
    const auto found = windows.find(hwnd);
    if (found == windows.end() || refuse.count(hwnd)) return {};
    // The window sat on a desktop that is not shown: a real placer would touch a cloaked window.
    if (found->second.cloaked) cloakedPlacements.push_back(hwnd);
    QRect achieved = slot;
    if (clamp) achieved = clamp(hwnd, slot);
    found->second.rect = achieved;
    return {true, achieved};
  }
  void activate(WindowId hwnd) override {
    activated.push_back(hwnd);
    front = hwnd;
  }

  bool close(WindowId hwnd) override {
    closed.push_back(hwnd);
    return windows.count(hwnd) != 0;
  }
  bool minimize(WindowId hwnd) override {
    minimized.push_back(hwnd);
    return windows.count(hwnd) != 0;
  }

  // The slots given to `hwnd`, oldest first.
  std::vector<QRect> slotsOf(WindowId hwnd) const {
    std::vector<QRect> out;
    for (const Placement& p : placements) {
      if (p.hwnd == hwnd) out.push_back(p.slot);
    }
    return out;
  }
  QRect lastSlot(WindowId hwnd) const {
    const std::vector<QRect> all = slotsOf(hwnd);
    return all.empty() ? QRect() : all.back();
  }

  std::map<WindowId, WindowFacts> windows;
  std::map<MonitorId, QRect> areas;
  QString desktop;
  std::optional<WindowId> front;
  std::set<WindowId> refuse;
  std::function<QRect(WindowId, const QRect&)> clamp;
  std::vector<Placement> placements;
  std::vector<WindowId> cloakedPlacements;
  std::vector<WindowId> activated;
  std::vector<WindowId> closed;
  std::vector<WindowId> minimized;
  int queries = 0;
};

}  // namespace waku::tiling::testing

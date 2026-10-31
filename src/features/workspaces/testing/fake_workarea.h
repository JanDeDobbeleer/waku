#pragma once

// Fakes for KomorebiWorkAreaGuard: a notifier that records each call and returns a scripted
// result, and a clock whose timers fire only when a test advances it. No test sends a message to
// the real komorebi.

#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "features/workspaces/komorebi_workarea.h"

namespace wabar::workspaces::komorebi {

struct FakeWorkAreaLog {
  // The fake clock's time (ms) of every notify() call.
  std::vector<int> calls;
  WorkAreaNotifyResult result{true, true, 0x1234, 0};
};

class FakeWorkAreaNotifier final : public WorkAreaNotifier {
 public:
  FakeWorkAreaNotifier(std::shared_ptr<FakeWorkAreaLog> log, std::shared_ptr<int> now)
      : log_(std::move(log)), now_(std::move(now)) {}
  WorkAreaNotifyResult notify() override {
    log_->calls.push_back(*now_);
    return log_->result;
  }

 private:
  std::shared_ptr<FakeWorkAreaLog> log_;
  std::shared_ptr<int> now_;
};

// Single-shot timers on a manual clock. advance() moves the time forward and fires every timer
// that falls due, in deadline order, each at its own deadline.
class FakeWorkAreaClock final : public WorkAreaTimers {
 public:
  explicit FakeWorkAreaClock(std::shared_ptr<int> now) : now_(std::move(now)) {}
  void start(Slot slot, int ms, std::function<void()> fire) override {
    pending_[slot] = {*now_ + ms, std::move(fire)};
  }
  void stop(Slot slot) override { pending_.erase(slot); }
  bool active(Slot slot) const override { return pending_.count(slot) != 0; }

  void advance(int ms) {
    const int end = *now_ + ms;
    for (;;) {
      auto due = pending_.end();
      for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->second.first <= end &&
            (due == pending_.end() || it->second.first < due->second.first)) {
          due = it;
        }
      }
      if (due == pending_.end()) break;
      *now_ = due->second.first;
      std::function<void()> fire = std::move(due->second.second);
      pending_.erase(due);
      fire();
    }
    *now_ = end;
  }

 private:
  std::shared_ptr<int> now_;
  std::map<Slot, std::pair<int, std::function<void()>>> pending_;
};

// A guard on the fakes, with the default timing (500 ms settle, 5 s late).
struct WorkAreaRig {
  std::shared_ptr<int> now = std::make_shared<int>(0);
  std::shared_ptr<FakeWorkAreaLog> log = std::make_shared<FakeWorkAreaLog>();
  FakeWorkAreaClock* clock = nullptr;
  std::unique_ptr<KomorebiWorkAreaGuard> guard;

  WorkAreaRig() {
    auto timers = std::make_unique<FakeWorkAreaClock>(now);
    clock = timers.get();
    guard = std::make_unique<KomorebiWorkAreaGuard>(
        std::make_unique<FakeWorkAreaNotifier>(log, now), std::move(timers));
  }
};

}  // namespace wabar::workspaces::komorebi

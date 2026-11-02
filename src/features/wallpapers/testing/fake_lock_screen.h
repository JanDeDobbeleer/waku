// A lock screen setter that records what it was asked to show, so no test touches the real lock
// screen. `fail` makes set() throw, as a broken setter might.
#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <stdexcept>
#include <utility>

#include "features/wallpapers/lock_screen.h"

namespace waku::wallpapers {

struct LockScreenLog {
  QStringList sets;
  bool fail = false;
};

class FakeLockScreen final : public LockScreenSetter {
 public:
  explicit FakeLockScreen(std::shared_ptr<LockScreenLog> log) : log_(std::move(log)) {}
  void set(const QString& path) override {
    log_->sets << path;
    if (log_->fail) throw std::runtime_error("fake lock screen failure");
  }

 private:
  std::shared_ptr<LockScreenLog> log_;
};

}  // namespace waku::wallpapers

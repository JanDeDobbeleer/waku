#pragma once

#include <memory>
#include <utility>

namespace wabar::kernel {

// Keeps `held` alive for `ms` more milliseconds, then lets it go on the event loop of the
// application's thread. Without a QCoreApplication, or with `ms` <= 0, it is let go at once. At
// exit a pending release runs when the application is destroyed.
void linger(std::shared_ptr<void> held, int ms);

// The grace period of the reload hubs: longer than any reload.
constexpr int kReloadLingerMs = 30000;

// One process-wide object of a kind (a "hub": GSMTC, the notification listener), shared by every
// holder, that outlives its last holder by `ms`.
//
// A reload deletes the QML singletons and creates new ones a moment later. A hub behind a plain
// weak_ptr is stopped then (its thread joined on the GUI thread, in the middle of the reload) and
// started again right after. Here the last holder's release only starts the grace period, and a
// holder that comes within it gets the running hub. A hub nobody asks for again goes when the time
// is up. GUI thread only.
template <typename T>
class LingeringHub {
 public:
  explicit LingeringHub(int ms = kReloadLingerMs) : ms_(ms) {}

  // The running hub, or a new one from `make()`.
  template <typename Make>
  std::shared_ptr<T> acquire(Make&& make) {
    if (std::shared_ptr<T> handle = handle_.lock()) return handle;
    std::shared_ptr<T> owner = owner_.lock();  // still in its grace period
    if (!owner) {
      owner = std::forward<Make>(make)();
      if (!owner) return nullptr;
      owner_ = owner;
    }
    // Holders share the handle; its deleter hands the owning reference to linger().
    std::shared_ptr<T> handle(owner.get(),
                              [owner, ms = ms_](T*) mutable { linger(std::move(owner), ms); });
    handle_ = handle;
    return handle;
  }

 private:
  int ms_;
  std::weak_ptr<T> handle_;
  std::weak_ptr<T> owner_;
};

}  // namespace wabar::kernel

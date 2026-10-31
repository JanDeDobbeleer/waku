#include "kernel/qt/linger.h"

#include <QCoreApplication>
#include <QTimer>
#include <utility>

namespace wabar::kernel {

void linger(std::shared_ptr<void> held, int ms) {
  if (!held) return;
  QCoreApplication* app = QCoreApplication::instance();
  if (!app || ms <= 0) return;  // `held` goes here
  // The functor owns the reference; destroying the pending timer (application exit) drops it too.
  QTimer::singleShot(ms, app, [held = std::move(held)]() mutable { held.reset(); });
}

}  // namespace wabar::kernel

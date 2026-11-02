#include "features/desktop_state/qml/idle_inhibitor.h"

namespace waku::desktop_state {

IdleInhibitor::IdleInhibitor(QObject* parent)
    : IdleInhibitor(&systemIdleInhibitCounter(), parent) {}

IdleInhibitor::IdleInhibitor(IdleInhibitCounter* counter, QObject* parent)
    : QObject(parent), counter_(counter) {}

IdleInhibitor::~IdleInhibitor() {
  if (enabled_) counter_->release();
}

void IdleInhibitor::setEnabled(bool enabled) {
  if (enabled == enabled_) return;
  enabled_ = enabled;
  if (enabled_) {
    counter_->acquire();
  } else {
    counter_->release();
  }
  emit enabledChanged();
}

void IdleInhibitor::setWindow(QObject* window) {
  if (window == window_) return;
  window_ = window;
  emit windowChanged();
}

}  // namespace waku::desktop_state

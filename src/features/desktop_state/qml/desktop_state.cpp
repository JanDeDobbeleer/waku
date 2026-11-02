#include "features/desktop_state/qml/desktop_state.h"

#include <cmath>
#include <utility>

#include "kernel/qt/live_desktop.h"

namespace waku::desktop_state {

DesktopState::DesktopState(QObject* parent)
    : DesktopState(kernel::liveDesktop() ? createSystemDesktopProbe() : nullptr, parent) {}

DesktopState::DesktopState(std::unique_ptr<DesktopProbe> probe, QObject* parent)
    : QObject(parent), probe_(std::move(probe)) {
  timer_.setInterval(kPollIntervalMs);
  // A destroyed QML binding does not call disconnectNotify, so the tick also checks that a signal
  // is still connected. QML keeps a conservative per-signal mask, so once a binding has observed a
  // signal the timer runs until the singleton is destroyed; C++ connections stop it precisely.
  connect(&timer_, &QTimer::timeout, this, [this] {
    poll(true);
    if (!observed()) timer_.stop();
  });
  poll(false);
}

DesktopState::~DesktopState() = default;

bool DesktopState::capsLock() {
  freshen();
  return keys_.caps;
}

bool DesktopState::numLock() {
  freshen();
  return keys_.num;
}

bool DesktopState::scrollLock() {
  freshen();
  return keys_.scroll;
}

QString DesktopState::keyboardLayout() {
  freshen();
  return layout_.shortName;
}

QString DesktopState::keyboardLayoutName() {
  freshen();
  return layout_.displayName;
}

QString DesktopState::keyboardLayoutId() {
  freshen();
  return layout_.id;
}

int DesktopState::idleSeconds() {
  freshen();
  return idleSeconds_;
}

void DesktopState::refresh() { poll(true); }

void DesktopState::freshen() {
  if (!timer_.isActive()) poll(false);
}

void DesktopState::poll(bool emitSignals) {
  if (!probe_) return;
  const LockKeys keys = probe_->lockKeys();
  const KeyboardLayoutInfo layout = probe_->keyboardLayout();
  const int idle = static_cast<int>(std::floor(probe_->idleSeconds().value_or(0.0)));
  const LockKeys oldKeys = std::exchange(keys_, keys);
  const bool layoutChanged = layout != layout_;
  layout_ = layout;
  const bool idleChanged = idle != idleSeconds_;
  idleSeconds_ = idle;
  if (!emitSignals) return;
  if (keys.caps != oldKeys.caps) emit capsLockChanged();
  if (keys.num != oldKeys.num) emit numLockChanged();
  if (keys.scroll != oldKeys.scroll) emit scrollLockChanged();
  if (layoutChanged) emit keyboardLayoutChanged();
  if (idleChanged) emit idleSecondsChanged();
}

bool DesktopState::observed() const {
  for (const QMetaMethod& signal : {QMetaMethod::fromSignal(&DesktopState::capsLockChanged),
                                    QMetaMethod::fromSignal(&DesktopState::numLockChanged),
                                    QMetaMethod::fromSignal(&DesktopState::scrollLockChanged),
                                    QMetaMethod::fromSignal(&DesktopState::keyboardLayoutChanged),
                                    QMetaMethod::fromSignal(&DesktopState::idleSecondsChanged)}) {
    if (isSignalConnected(signal)) return true;
  }
  return false;
}

// connectNotify and disconnectNotify run on the thread that connects or disconnects, so the work
// is queued to this object's thread.
void DesktopState::connectNotify(const QMetaMethod&) {
  QMetaObject::invokeMethod(this, [this] { updatePolling(); }, Qt::QueuedConnection);
}

void DesktopState::disconnectNotify(const QMetaMethod&) {
  QMetaObject::invokeMethod(this, [this] { updatePolling(); }, Qt::QueuedConnection);
}

void DesktopState::updatePolling() {
  if (!probe_) return;
  const bool watch = observed();
  if (watch && !timer_.isActive()) {
    poll(true);
    timer_.start();
  } else if (!watch && timer_.isActive()) {
    timer_.stop();
  }
}

}  // namespace waku::desktop_state

#include "host/mask_poller.h"

#include <QScreen>

namespace wabar::host {

// ---- MaskPollerHub ----

MaskPollerHub::MaskPollerHub(CursorSource cursor, QObject* parent)
    : QObject(parent), cursor_(std::move(cursor)) {
  elapsed_.start();
  clock_ = [this] { return elapsed_.elapsed(); };
  timer_.setTimerType(Qt::PreciseTimer);
  timer_.setInterval(kFastIntervalMs);
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
}

MaskPollerHub::~MaskPollerHub() { timer_.stop(); }

std::optional<QPoint> MaskPollerHub::readCursor() {
  if (!cursor_) return std::nullopt;
  ++cursorReads_;
  return cursor_();
}

void MaskPollerHub::add(MaskPoller* poller) {
  for (const QPointer<MaskPoller>& p : pollers_) {
    if (p == poller) return;
  }
  pollers_.append(poller);
  ensureTimer();
}

void MaskPollerHub::remove(MaskPoller* poller) {
  pollers_.removeIf([poller](const QPointer<MaskPoller>& p) { return !p || p == poller; });
  if (pollers_.isEmpty()) timer_.stop();
}

void MaskPollerHub::ensureTimer() {
  if (timer_.isActive() || pollers_.isEmpty()) return;
  // A new poller starts at the fast rate: something just changed.
  lastMoveMs_ = clock_();
  timer_.setInterval(kFastIntervalMs);
  timer_.start();
}

void MaskPollerHub::tick() {
  pollers_.removeIf([](const QPointer<MaskPoller>& p) { return !p; });
  if (pollers_.isEmpty()) {
    timer_.stop();
    return;
  }
  const std::optional<QPoint> cursor = readCursor();
  const qint64 now = clock_();
  bool moved = false;
  if (cursor && (!lastCursor_ || *cursor != *lastCursor_)) {
    moved = true;
    lastCursor_ = cursor;
    lastMoveMs_ = now;
  }
  const bool recheck = now - lastRecheckMs_ >= kRecheckMs;
  if (recheck) lastRecheckMs_ = now;
  // A poller may leave the hub while it runs (its window hid): walk a copy.
  const QList<QPointer<MaskPoller>> pollers = pollers_;
  for (const QPointer<MaskPoller>& poller : pollers) {
    if (poller) poller->tick(cursor, moved || recheck);
  }
  if (pollers_.isEmpty()) {
    timer_.stop();
    return;
  }
  const int interval = now - lastMoveMs_ < kActiveWindowMs ? kFastIntervalMs : kIdleIntervalMs;
  if (timer_.interval() != interval) timer_.setInterval(interval);
}

// ---- MaskPoller ----

MaskPoller::MaskPoller(QWindow* window, MaskPollerHub* hub, MaskSource mask, QObject* parent)
    : QObject(parent), window_(window), hub_(hub), mask_(std::move(mask)) {}

MaskPoller::~MaskPoller() {
  leave();
  setPassThrough(false);
}

void MaskPoller::leave() {
  registered_ = false;
  if (hub_) hub_->remove(this);
}

void MaskPoller::refresh() {
  const bool wanted = window_ && window_->isVisible() && hub_ && mask_ && mask_().has_value();
  if (!wanted) {
    leave();
    setPassThrough(false);
    return;
  }
  if (!registered_) {
    registered_ = true;
    dirty_ = true;
    hub_->add(this);
    evaluate(hub_->readCursor());
  }
}

bool MaskPoller::poll() {
  refresh();
  if (registered_) evaluate(hub_->readCursor());
  return passThrough_;
}

void MaskPoller::tick(const std::optional<QPoint>& cursor, bool force) {
  if (!window_ || !window_->isVisible()) {
    refresh();
    return;
  }
  if (!cursor) return;
  if (!force && !dirty_ && window_->geometry() == lastGeometry_) return;
  evaluate(cursor);
}

void MaskPoller::evaluate(const std::optional<QPoint>& physical) {
  if (!window_ || !physical) return;
  const std::optional<RegionNode> mask = mask_();
  if (!mask) {
    refresh();
    return;
  }
  if (hub_) ++hub_->evaluations_;
  dirty_ = false;
  lastGeometry_ = window_->geometry();
  const QScreen* screen = window_->screen();
  const QPointF local =
      cursorInWindow(*physical, screen ? screen->geometry().topLeft() : QPoint(),
                     screen ? screen->devicePixelRatio() : 1.0, window_->geometry().topLeft());
  // Outside the window nothing is hit anyway; the answer then follows the region test, which has
  // the window already click-through there unless the cursor is within the pad of a region.
  setPassThrough(!maskAcceptsInput(mask, local, kPadPx, window_->size()));
}

void MaskPoller::setPassThrough(bool passThrough) {
  if (passThrough_ == passThrough) return;
  passThrough_ = passThrough;
  ++flips_;
  if (window_) window_->setFlag(Qt::WindowTransparentForInput, passThrough);
}

}  // namespace wabar::host

#include "host/mask_poller.h"

#include <QScreen>
#include <algorithm>
#include <limits>

namespace waku::host {

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
    if (poller) poller->tick(cursor, moved || recheck, recheck);
  }
  if (pollers_.isEmpty()) {
    timer_.stop();
    return;
  }
  // Each poller's distance is from this cursor position: a moved cursor evaluated all of them,
  // a resting one changes no distance unless the poller was dirty, and then it was evaluated.
  qreal safe = cursor ? std::numeric_limits<qreal>::infinity() : 0.0;
  for (const QPointer<MaskPoller>& poller : pollers_) {
    if (poller) safe = std::min(safe, poller->safeDistance());
  }
  const int floor = cursorActive(now) ? kFastIntervalMs : kIdleIntervalMs;
  const qreal reachMs = safe / kMaxCursorSpeed;
  const int interval =
      reachMs >= kSlowIntervalMs ? kSlowIntervalMs : std::max(floor, static_cast<int>(reachMs));
  if (timer_.interval() != interval) timer_.setInterval(interval);
}

void MaskPollerHub::noteDirty() {
  if (!timer_.isActive()) return;
  const int cap = cursorActive(clock_()) ? kFastIntervalMs : kIdleIntervalMs;
  if (timer_.remainingTime() > cap) timer_.start(cap);
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

void MaskPoller::markDirty() {
  dirty_ = true;
  if (registered_ && hub_) hub_->noteDirty();
}

void MaskPoller::refresh() {
  bool wanted = window_ && window_->isVisible() && hub_ && mask_;
  if (wanted) {
    // The build that answers "is there a mask" is the one the first evaluation uses.
    node_ = mask_();
    built_ = true;
    ++maskBuilds_;
    wanted = node_.has_value();
  }
  if (!wanted) {
    leave();
    setPassThrough(false);
    return;
  }
  if (!registered_) {
    registered_ = true;
    dirty_ = true;
    hub_->add(this);
    evaluate(hub_->readCursor(), false);
  }
}

bool MaskPoller::poll() {
  refresh();
  if (registered_) evaluate(hub_->readCursor(), false);
  return passThrough_;
}

void MaskPoller::tick(const std::optional<QPoint>& cursor, bool force, bool rebuild) {
  if (!window_ || !window_->isVisible()) {
    refresh();
    return;
  }
  if (!cursor) return;
  const bool changed = dirty_ || window_->geometry() != lastGeometry_;
  if (!force && !changed) return;
  evaluate(cursor, rebuild || changed);
}

void MaskPoller::evaluate(const std::optional<QPoint>& physical, bool rebuild) {
  if (!window_ || !physical) return;
  if (rebuild || !built_) {
    node_ = mask_();
    built_ = true;
    ++maskBuilds_;
  }
  if (!node_) {
    refresh();
    return;
  }
  if (hub_) ++hub_->evaluations_;
  dirty_ = false;
  lastGeometry_ = window_->geometry();
  const QScreen* screen = window_->screen();
  const qreal dpr = screen && screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
  const QPointF local = cursorInWindow(*physical, screen ? screen->geometry().topLeft() : QPoint(),
                                       dpr, window_->geometry().topLeft());
  // Outside the window nothing is hit anyway; the answer then follows the region test, which has
  // the window already click-through there unless the cursor is within the pad of a region.
  setPassThrough(!maskAcceptsInput(node_, local, kPadPx, window_->size()));
  // The padded test samples points kPadPx away, so an edge counts that much nearer; one more
  // logical pixel covers rounding. Logical to physical pixels is the screen's ratio.
  const qreal edge = regionEdgeDistance(*node_, local, window_->size());
  safeDistance_ = std::max(0.0, edge - kPadPx - 1) * dpr;
}

void MaskPoller::setPassThrough(bool passThrough) {
  if (passThrough_ == passThrough) return;
  passThrough_ = passThrough;
  ++flips_;
  if (window_) window_->setFlag(Qt::WindowTransparentForInput, passThrough);
}

}  // namespace waku::host

#include "host/mask_poller.h"

#include <QScreen>

namespace wabar::host {

MaskPoller::MaskPoller(QWindow* window, CursorSource cursor, MaskSource mask, QObject* parent)
    : QObject(parent), window_(window), cursor_(std::move(cursor)), mask_(std::move(mask)) {
  timer_.setTimerType(Qt::PreciseTimer);
  timer_.setInterval(kIntervalMs);
  connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}

MaskPoller::~MaskPoller() {
  timer_.stop();
  setPassThrough(false);
}

void MaskPoller::refresh() {
  const bool wanted = window_ && window_->isVisible() && cursor_ && mask_ && mask_().has_value();
  if (!wanted) {
    timer_.stop();
    setPassThrough(false);
    return;
  }
  if (!timer_.isActive()) {
    timer_.start();
    poll();
  }
}

bool MaskPoller::poll() {
  if (!window_ || !window_->isVisible() || !cursor_) {
    refresh();
    return passThrough_;
  }
  const std::optional<RegionNode> mask = mask_();
  if (!mask) {
    refresh();
    return passThrough_;
  }
  const std::optional<QPoint> physical = cursor_();
  if (!physical) return passThrough_;
  const QScreen* screen = window_->screen();
  const QPointF local =
      cursorInWindow(*physical, screen ? screen->geometry().topLeft() : QPoint(),
                     screen ? screen->devicePixelRatio() : 1.0, window_->geometry().topLeft());
  // Outside the window nothing is hit anyway; the answer then follows the region test, which has
  // the window already click-through there unless the cursor is within the pad of a region.
  setPassThrough(!maskAcceptsInput(mask, local, kPadPx, window_->size()));
  return passThrough_;
}

void MaskPoller::setPassThrough(bool passThrough) {
  if (passThrough_ == passThrough) return;
  passThrough_ = passThrough;
  ++flips_;
  if (window_) window_->setFlag(Qt::WindowTransparentForInput, passThrough);
}

}  // namespace wabar::host

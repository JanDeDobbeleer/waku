#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWindow>
#include <functional>
#include <optional>

#include "host/region_math.h"

namespace wabar::host {

// The cursor position in physical (native) screen pixels, or nothing when it cannot be read.
// The real source is GetCursorPos (ShellHost::NativeHooks::win32()); tests pass a fake.
using CursorSource = std::function<std::optional<QPoint>()>;

class MaskPoller;

// The one timer behind every window mask. Windows has no per-pixel hit test API a Qt window can
// use, so while a masked window is visible the hub reads the cursor once per tick and hands it to
// each registered MaskPoller, which tests it against its window's mask grown by kPadPx.
//
// Rate: kFastIntervalMs (120 Hz) while the cursor moved within kActiveWindowMs, kIdleIntervalMs
// (30 Hz) otherwise. A tick where the cursor did not move evaluates only the pollers whose window
// moved, repainted or whose region changed since their last evaluation (MaskPoller::markDirty),
// plus every poller once per kRecheckMs as a safety net. With no registered poller the timer is
// stopped.
//
// The known race: a click in the gap between the cursor entering a region and the next tick (up to
// kIdleIntervalMs after a still cursor starts moving, then at most kFastIntervalMs) goes to the
// window below. The padding makes the flip happen before the cursor reaches the region, which
// covers a normal approach.
//
// Threading: GUI thread only. The hub owns its timer; it does not own the pollers or windows.
class MaskPollerHub : public QObject {
  Q_OBJECT

 public:
  // 120 Hz. A flip costs 2-4 ms, a cursor read well under a millisecond.
  static constexpr int kFastIntervalMs = 8;
  // 30 Hz while the cursor rests.
  static constexpr int kIdleIntervalMs = 33;
  // How long after the last cursor movement the fast rate stays.
  static constexpr int kActiveWindowMs = 300;
  // Every poller is evaluated at least this often even when nothing is known to have changed.
  static constexpr int kRecheckMs = 1000;

  using Clock = std::function<qint64()>;  // milliseconds, monotonic

  explicit MaskPollerHub(CursorSource cursor, QObject* parent = nullptr);
  ~MaskPollerHub() override;

  // Replaces the millisecond clock (tests). The default is a QElapsedTimer.
  void setClock(Clock clock) { clock_ = std::move(clock); }

  // One tick: what the timer does. Public so tests drive it by hand.
  void tick();

  bool running() const { return timer_.isActive(); }
  int intervalMs() const { return timer_.interval(); }
  // How many pollers are registered, how many cursor reads and mask evaluations were made.
  int pollerCount() const { return static_cast<int>(pollers_.size()); }
  int cursorReads() const { return cursorReads_; }
  int evaluations() const { return evaluations_; }

 private:
  friend class MaskPoller;
  void add(MaskPoller* poller);
  void remove(MaskPoller* poller);
  std::optional<QPoint> readCursor();
  void ensureTimer();

  CursorSource cursor_;
  Clock clock_;
  QElapsedTimer elapsed_;
  QTimer timer_;
  QList<QPointer<MaskPoller>> pollers_;
  std::optional<QPoint> lastCursor_;
  qint64 lastMoveMs_ = -1000000;
  qint64 lastRecheckMs_ = 0;
  int cursorReads_ = 0;
  int evaluations_ = 0;
};

// Gives one window an input region. While the window is visible and has a mask, the hub feeds it
// the cursor; outside the padded region the window is made transparent for input
// (Qt::WindowTransparentForInput, which Qt maps to WS_EX_LAYERED | WS_EX_TRANSPARENT), so clicks
// fall through to the window below; inside it, the flag is cleared and the window takes input.
// The flag is set only when the answer changes.
//
// Threading: GUI thread only. The poller does not own the window, the mask or the hub. It leaves
// the hub when the window hides or the mask goes, and restores the flag then.
class MaskPoller : public QObject {
  Q_OBJECT

 public:
  // Logical pixels around each region that already count as inside.
  static constexpr qreal kPadPx = 6;

  using MaskSource = std::function<std::optional<RegionNode>()>;

  MaskPoller(QWindow* window, MaskPollerHub* hub, MaskSource mask, QObject* parent = nullptr);
  ~MaskPoller() override;

  // Joins or leaves the hub to match the window (visible) and the mask (set). Call it when either
  // changes. Joining evaluates once at once.
  void refresh();
  // One forced evaluation with a fresh cursor read. Returns whether the window is now transparent
  // for input.
  bool poll();

 public slots:
  // Tells the poller that the mask, the window or what the window shows may have changed, so the
  // next tick evaluates it even if the cursor rests.
  void markDirty() { dirty_ = true; }

 public:
  bool running() const { return registered_; }
  bool passThrough() const { return passThrough_; }
  // How many times the window flag was changed.
  int flips() const { return flips_; }

 private:
  friend class MaskPollerHub;
  // The hub's per-tick call: evaluates when `force`, when dirty or when the window moved. It does
  // not build the mask unless it evaluates.
  void tick(const std::optional<QPoint>& cursor, bool force);
  void evaluate(const std::optional<QPoint>& cursor);
  void leave();
  void setPassThrough(bool passThrough);

  QPointer<QWindow> window_;
  QPointer<MaskPollerHub> hub_;
  MaskSource mask_;
  bool registered_ = false;
  bool dirty_ = true;
  QRect lastGeometry_;
  bool passThrough_ = false;
  int flips_ = 0;
};

}  // namespace wabar::host

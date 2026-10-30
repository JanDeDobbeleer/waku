#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <functional>
#include <optional>

#include "host/region_math.h"

namespace wabar::host {

// The cursor position in physical (native) screen pixels, or nothing when it cannot be read.
// The real source is GetCursorPos (ShellHost::NativeHooks::win32()); tests pass a fake.
using CursorSource = std::function<std::optional<QPoint>()>;

// Gives a window an input region on Windows, which has no per-pixel hit test API a Qt window can
// use. While the window is visible and has a mask, a timer reads the cursor and tests it against
// the mask grown by kPadPx. Outside that, the window is made transparent for input
// (Qt::WindowTransparentForInput, which Qt maps to WS_EX_LAYERED | WS_EX_TRANSPARENT), so clicks
// fall through to the window below; inside it, the flag is cleared and the window takes input.
// The flag is set only when the answer changes.
//
// The known race: a click in the gap between the cursor entering a region and the next poll
// (at most kIntervalMs) goes to the window below. The padding makes the flip happen before the
// cursor reaches the region, which covers a normal approach.
//
// Threading: GUI thread only. The poller owns its timer; it does not own the window or the mask.
// It stops when the window hides or the mask goes, and restores the flag when it stops.
class MaskPoller : public QObject {
  Q_OBJECT

 public:
  // 120 Hz. A flip costs 2-4 ms, a cursor read well under a millisecond.
  static constexpr int kIntervalMs = 8;
  // Logical pixels around each region that already count as inside.
  static constexpr qreal kPadPx = 6;

  using MaskSource = std::function<std::optional<RegionNode>()>;

  MaskPoller(QWindow* window, CursorSource cursor, MaskSource mask, QObject* parent = nullptr);
  ~MaskPoller() override;

  // Starts or stops the timer to match the window (visible) and the mask (set). Call it when
  // either changes. Starting polls once at once.
  void refresh();
  // One poll. Returns whether the window is now transparent for input. The timer calls it.
  bool poll();

  bool running() const { return timer_.isActive(); }
  bool passThrough() const { return passThrough_; }
  // How many times the window flag was changed.
  int flips() const { return flips_; }

 private:
  void setPassThrough(bool passThrough);

  QPointer<QWindow> window_;
  CursorSource cursor_;
  MaskSource mask_;
  QTimer timer_;
  bool passThrough_ = false;
  int flips_ = 0;
};

}  // namespace wabar::host

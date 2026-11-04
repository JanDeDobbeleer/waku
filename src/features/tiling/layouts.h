#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <cstddef>
#include <optional>
#include <vector>

namespace waku::tiling {

// Pure geometry: where the n windows of a workspace go. Slots are the visible bounds of a window.

enum class Direction { Left, Right, Up, Down };
enum class Axis { Horizontal, Vertical };

// The layouts, in the order a cycle visits them.
QStringList layoutNames();
// The canonical name of `name` ("mainStack", "main-stack" and "MAIN_STACK" all give "mainStack");
// empty for an unknown layout.
QString canonicalLayout(const QString& name);

// The slots for `count` windows inside `area` (the work area), in stack order. `gap` is the space
// between windows and between the windows and the edge of the area; `ratio` is the share of the
// width the main window takes (clamped to 0.1..0.9). An unknown layout is mainStack.
//   mainStack  one window fills the area; with more, the first takes the left `ratio` of the
//              width and the others share the right column, stacked top to bottom
//   columns    equal columns side by side
// `weights` (optional, one per window in stack order) sizes the stacked windows of mainStack
// against each other by height; the entry of the main window and a missing entry count as 1.
// The slots do not overlap and lie inside `area`; the last slot of a row or column absorbs the
// rounding remainder. A gap larger than the area leaves 1 pixel slots instead of negative ones.
std::vector<QRect> layoutSlots(const QString& layout, const QRect& area, int count, int gap,
                               double ratio, const std::vector<double>& weights = {});

// Mirrors `rects` inside `area`: left to right when `horizontal`, top to bottom when `vertical`.
std::vector<QRect> flipSlots(std::vector<QRect> rects, const QRect& area, bool horizontal,
                             bool vertical);

// The slot nearest to slot `from` in `direction`, by the distance of the centres with the
// sideways offset counting double (so "right" of a tall main window is the stack window at its
// height); nothing when no slot lies that way.
std::optional<size_t> neighbourSlot(const std::vector<QRect>& rects, size_t from,
                                    Direction direction);

// The rectangle to give SetWindowPos so that the visible bounds become `slot`: Windows 10 and 11
// draw a frame of a few invisible pixels around a resizable window, so `windowRect`
// (GetWindowRect) is larger than `visibleRect` (DWMWA_EXTENDED_FRAME_BOUNDS). The slot grows by
// the difference on each edge.
QRect outerRect(const QRect& slot, const QRect& windowRect, const QRect& visibleRect);

}  // namespace waku::tiling

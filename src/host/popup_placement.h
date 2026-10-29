#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace wabar::host {

// Which side of the anchor a popup prefers.
enum class PopupEdge { Below, Above };

// Where a popup or tooltip goes, as a pure function so it can be tested without windows. The
// top-left corner for a popup of `size`, centred on `anchor` on the preferred side at `gap` from
// it. The popup stays `margin` inside `screen`: it slides sideways to fit and flips to the other
// side of the anchor when the preferred side has no room (and, if neither side fits, is clamped
// into the screen). All coordinates are logical pixels in the same (global) space.
QPointF placePopup(const QRectF& anchor, const QSizeF& size, const QRectF& screen, PopupEdge edge,
                   qreal gap, qreal margin);

}  // namespace wabar::host

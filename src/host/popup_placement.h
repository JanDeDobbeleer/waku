#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <optional>

#include "host/app_bar.h"

namespace waku::host {

// Which side of the anchor a popup prefers. A top panel opens Below, a bottom panel Above, a left
// panel Right and a right panel Left.
enum class PopupEdge { Below, Above, Right, Left };

// Where a popup or tooltip goes, as a pure function so it can be tested without windows. The
// top-left corner for a popup of `size`, centred on `anchor` on the preferred side at `gap` from
// it. The popup stays `margin` inside `screen`: it slides along the anchor's side to fit and flips
// to the opposite side of the anchor when the preferred side has no room (and, if neither side
// fits, is clamped into the screen). All coordinates are logical pixels in the same (global)
// space.
QPointF placePopup(const QRectF& anchor, const QSizeF& size, const QRectF& screen, PopupEdge edge,
                   qreal gap, qreal margin);

// The side a popup opens on for a panel docked to `edge`: Top opens Below, Bottom Above, Left
// Right, Right Left. A window that is not docked opens Below.
PopupEdge popupEdgeForDock(std::optional<AppBarEdge> edge);

// The rectangle a popup anchors to: `item` (an item's rectangle in global coordinates) extended
// across the thickness of its panel (`panel`, the panel window's rectangle in the same space). On
// a horizontal panel that is the item's x range over the panel's y range, so the popup opens just
// outside the panel and not just outside the item; on a vertical panel the item's y range over
// the panel's x range.
QRectF panelAnchorRect(const QRectF& item, const QRectF& panel, bool vertical);

}  // namespace waku::host

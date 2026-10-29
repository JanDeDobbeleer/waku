#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace wabar {

// Where a popup or tooltip goes, as a pure function so it can be tested without windows.
// Used by PopupWindow and Tooltip as `PopupPlacement.position(...)`; all coordinates are logical
// pixels in the same (global) space.
class PopupPlacement : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  // Which side of the anchor the popup prefers.
  enum Edge { Below, Above };
  Q_ENUM(Edge)

  explicit PopupPlacement(QObject* parent = nullptr) : QObject(parent) {}

  // The top-left corner for a popup of `size`, centred on `anchor` on the preferred side at `gap`
  // from it. The popup stays `margin` inside `screen`: it slides sideways to fit and flips to the
  // other side of the anchor when the preferred side has no room (and, if neither side fits, is
  // clamped into the screen).
  static QPointF place(const QRectF& anchor, const QSizeF& size, const QRectF& screen, Edge edge,
                       qreal gap, qreal margin);

  Q_INVOKABLE QPointF position(const QRectF& anchor, const QSizeF& size, const QRectF& screen,
                               int edge, qreal gap, qreal margin) const {
    return place(anchor, size, screen, edge == Above ? Above : Below, gap, margin);
  }
};

}  // namespace wabar

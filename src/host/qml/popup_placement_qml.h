#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include "host/popup_placement.h"

namespace wabar {

// The QML face of placePopup(): used by PopupWindow and Tooltip as `PopupPlacement.position(...)`.
class PopupPlacement : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  // Which side of the anchor the popup prefers.
  enum Edge { Below, Above };
  Q_ENUM(Edge)

  explicit PopupPlacement(QObject* parent = nullptr) : QObject(parent) {}

  static QPointF place(const QRectF& anchor, const QSizeF& size, const QRectF& screen, Edge edge,
                       qreal gap, qreal margin) {
    return placePopup(anchor, size, screen, edge == Above ? PopupEdge::Above : PopupEdge::Below,
                      gap, margin);
  }

  Q_INVOKABLE QPointF position(const QRectF& anchor, const QSizeF& size, const QRectF& screen,
                               int edge, qreal gap, qreal margin) const {
    return place(anchor, size, screen, edge == Above ? Above : Below, gap, margin);
  }
};

}  // namespace wabar

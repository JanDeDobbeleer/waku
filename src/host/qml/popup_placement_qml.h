#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSizeF>

#include "host/popup_placement.h"
#include "host/qml/window_enums.h"

namespace wabar::host {

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

// `anchor` of a PopupWindow (Quickshell `PopupAnchor`): what the popup opens next to.
//   item     the item it belongs to
//   rect     the part of `item` to open next to, in the item's coordinates; unset (empty): the
//            item across the whole thickness of its window, so the popup opens outside the bar
//   edges    the side of `rect` it opens on (Edges flags): Top opens above, Bottom below
//   gravity  the direction it grows from there; used for the side when `edges` names none
// wabar keeps its own placement: centred on the anchor, kept on screen, flipped when the side has
// no room. Unset edges and gravity: below, or above when the window is a bottom panel.
class PopupAnchor : public QObject {
  Q_OBJECT
  // Creatable only because PopupWindow.qml holds one; users set it through `anchor.item` and
  // friends.
  QML_ELEMENT
  Q_PROPERTY(QQuickItem* item READ item WRITE setItem NOTIFY changed FINAL)
  Q_PROPERTY(QRectF rect READ rect WRITE setRect NOTIFY changed FINAL)
  Q_PROPERTY(int edges READ edges WRITE setEdges NOTIFY changed FINAL)
  Q_PROPERTY(int gravity READ gravity WRITE setGravity NOTIFY changed FINAL)
  Q_PROPERTY(bool hasRect READ hasRect NOTIFY changed FINAL)

 public:
  explicit PopupAnchor(QObject* parent = nullptr) : QObject(parent) {}

  QQuickItem* item() const { return item_; }
  void setItem(QQuickItem* item) {
    if (item_ == item) return;
    item_ = item;
    emit changed();
  }
  QRectF rect() const { return rect_; }
  void setRect(const QRectF& rect) {
    if (rect_ == rect) return;
    rect_ = rect;
    emit changed();
  }
  int edges() const { return edges_; }
  void setEdges(int edges) {
    if (edges_ == edges) return;
    edges_ = edges;
    emit changed();
  }
  int gravity() const { return gravity_; }
  void setGravity(int gravity) {
    if (gravity_ == gravity) return;
    gravity_ = gravity;
    emit changed();
  }
  bool hasRect() const { return rect_.width() > 0 || rect_.height() > 0; }

  // The PopupPlacement side `edges` and `gravity` ask for, or `fallback` when they name none.
  Q_INVOKABLE int placementEdge(int fallback) const {
    for (int flags : {edges_, gravity_}) {
      if (flags & Edges::Top) return PopupPlacement::Above;
      if (flags & Edges::Bottom) return PopupPlacement::Below;
    }
    return fallback;
  }

 signals:
  void changed();

 private:
  QPointer<QQuickItem> item_;
  QRectF rect_;
  int edges_ = Edges::None;
  int gravity_ = Edges::None;
};

}  // namespace wabar::host

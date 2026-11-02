#pragma once

#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QQuickItem>
#include <optional>

#include "host/region_math.h"

namespace waku::host {

// How a Region combines with its parent (Quickshell `Intersection`).
namespace Intersection {
Q_NAMESPACE
QML_NAMED_ELEMENT(Intersection)
enum Enum { Combine = 0, Intersect = 1, Xor = 2, Subtract = 3 };
Q_ENUM_NS(Enum)
}  // namespace Intersection

namespace RegionShape {
Q_NAMESPACE
QML_NAMED_ELEMENT(RegionShape)
enum Enum { Rect = 0, Ellipse = 1 };
Q_ENUM_NS(Enum)
}  // namespace RegionShape

// A shape in window coordinates and the regions folded into it (Quickshell `Region`), the type of
// a window's `mask`. Only the points inside the mask take input; see MaskPoller for how that is
// done on Windows. The shape is the rectangle x, y, width, height, or the bounds of `item` when it
// is set. The child regions (the default property) are applied in order, each by its own
// `intersection`, so
//
//     mask: Region { x: 0; y: 0; width: 100; height: 40; Region { item: button } }
//
// takes input over the first rectangle and over the button. An empty `Region {}` takes no input at
// all. The `intersection` of the mask itself combines it with the window rectangle, as in
// Quickshell: Combine uses the mask alone; Intersect, Xor and Subtract give `window <op> mask`.
// So `intersection: Intersection.Xor` on the mask takes input everywhere except inside it.
//
// Threading: GUI thread only. Children are not owned; QML owns them.
class Region : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_CLASSINFO("DefaultProperty", "regions")
  Q_PROPERTY(qreal x MEMBER x_ NOTIFY xChanged FINAL)
  Q_PROPERTY(qreal y MEMBER y_ NOTIFY yChanged FINAL)
  Q_PROPERTY(qreal width MEMBER width_ NOTIFY widthChanged FINAL)
  Q_PROPERTY(qreal height MEMBER height_ NOTIFY heightChanged FINAL)
  Q_PROPERTY(waku::host::RegionShape::Enum shape MEMBER shape_ NOTIFY shapeChanged FINAL)
  Q_PROPERTY(QQuickItem* item READ item WRITE setItem NOTIFY itemChanged FINAL)
  Q_PROPERTY(waku::host::Intersection::Enum intersection MEMBER intersection_ NOTIFY
                 intersectionChanged FINAL)
  Q_PROPERTY(QQmlListProperty<waku::host::Region> regions READ regions FINAL)

 public:
  explicit Region(QObject* parent = nullptr) : QObject(parent) {
    connect(this, &Region::shapeChanged, this, &Region::treeChanged);
    connect(this, &Region::xChanged, this, &Region::treeChanged);
    connect(this, &Region::yChanged, this, &Region::treeChanged);
    connect(this, &Region::widthChanged, this, &Region::treeChanged);
    connect(this, &Region::heightChanged, this, &Region::treeChanged);
    connect(this, &Region::itemChanged, this, &Region::treeChanged);
    connect(this, &Region::intersectionChanged, this, &Region::treeChanged);
    connect(this, &Region::regionsChanged, this, &Region::treeChanged);
  }

  QQuickItem* item() const { return item_.data(); }
  void setItem(QQuickItem* item);
  QQmlListProperty<Region> regions();

  // The region as a value, in the coordinates of the window scene (an item's bounds are mapped
  // with mapRectToScene, so they follow the item as it moves).
  RegionNode node() const;

 signals:
  // One notify signal per property, so a binding can read one while writing another. (Moving the
  // item does not emit; the window's next frame tells the poller to read the region again.)
  void xChanged();
  void yChanged();
  void widthChanged();
  void heightChanged();
  void itemChanged();
  void intersectionChanged();
  void shapeChanged();
  void regionsChanged();
  // This region or any region below it changed (any property, item, intersection or the child
  // list). The mask poller listens to the root; an item that moves does not emit, the window's
  // frame request (afterAnimating) does.
  void treeChanged();

 private:
  static void appendRegion(QQmlListProperty<Region>* list, Region* region);
  static qsizetype regionCount(QQmlListProperty<Region>* list);
  static Region* regionAt(QQmlListProperty<Region>* list, qsizetype index);
  static void clearRegions(QQmlListProperty<Region>* list);

  qreal x_ = 0;
  qreal y_ = 0;
  qreal width_ = 0;
  qreal height_ = 0;
  RegionShape::Enum shape_ = RegionShape::Rect;
  Intersection::Enum intersection_ = Intersection::Combine;
  QPointer<QQuickItem> item_;
  QList<QPointer<Region>> regions_;
};

}  // namespace waku::host

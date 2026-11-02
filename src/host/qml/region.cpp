#include "host/qml/region.h"

namespace waku::host {

void Region::setItem(QQuickItem* item) {
  if (item_ == item) return;
  item_ = item;
  emit itemChanged();
}

QQmlListProperty<Region> Region::regions() {
  return QQmlListProperty<Region>(this, this, &Region::appendRegion, &Region::regionCount,
                                  &Region::regionAt, &Region::clearRegions);
}

void Region::appendRegion(QQmlListProperty<Region>* list, Region* region) {
  if (!region) return;
  auto* self = static_cast<Region*>(list->data);
  self->regions_.append(region);
  connect(region, &Region::treeChanged, self, &Region::treeChanged);
  emit self->regionsChanged();
}

qsizetype Region::regionCount(QQmlListProperty<Region>* list) {
  return static_cast<Region*>(list->data)->regions_.size();
}

Region* Region::regionAt(QQmlListProperty<Region>* list, qsizetype index) {
  return static_cast<Region*>(list->data)->regions_.at(index).data();
}

void Region::clearRegions(QQmlListProperty<Region>* list) {
  auto* self = static_cast<Region*>(list->data);
  self->regions_.clear();
  emit self->regionsChanged();
}

RegionNode Region::node() const {
  RegionNode node;
  if (item_) {
    node.rect = item_->mapRectToScene(QRectF(0, 0, item_->width(), item_->height()));
  } else {
    node.rect = QRectF(x_, y_, width_, height_);
  }
  node.shape = shape_ == RegionShape::Ellipse ? RegionShapeKind::Ellipse : RegionShapeKind::Rect;
  switch (intersection_) {
    case Intersection::Combine:
      node.op = RegionOp::Combine;
      break;
    case Intersection::Intersect:
      node.op = RegionOp::Intersect;
      break;
    case Intersection::Xor:
      node.op = RegionOp::Xor;
      break;
    case Intersection::Subtract:
      node.op = RegionOp::Subtract;
      break;
  }
  node.children.reserve(regions_.size());
  for (const QPointer<Region>& child : regions_) {
    if (child) node.children.append(child->node());
  }
  return node;
}

}  // namespace waku::host

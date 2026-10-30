#include "host/qml/transform_watcher.h"

#include <QSet>
#include <utility>

namespace wabar::host {

void TransformWatcher::setA(QQuickItem* item) {
  if (a_ == item) return;
  a_ = item;
  emit aChanged();
  rebuild();
  emit transformChanged();
}

void TransformWatcher::setB(QQuickItem* item) {
  if (b_ == item) return;
  b_ = item;
  emit bChanged();
  rebuild();
  emit transformChanged();
}

void TransformWatcher::setCommonParent(QQuickItem* item) {
  if (commonParent_ == item) return;
  commonParent_ = item;
  emit commonParentChanged();
  rebuild();
}

void TransformWatcher::listen(QQuickItem* item) {
  const auto changed = [this] { emit transformChanged(); };
  const auto rebuilt = [this] {
    rebuild();
    emit transformChanged();
  };
  connections_ << connect(item, &QQuickItem::xChanged, this, changed)
               << connect(item, &QQuickItem::yChanged, this, changed)
               << connect(item, &QQuickItem::widthChanged, this, changed)
               << connect(item, &QQuickItem::heightChanged, this, changed)
               << connect(item, &QQuickItem::scaleChanged, this, changed)
               << connect(item, &QQuickItem::rotationChanged, this, changed)
               << connect(item, &QQuickItem::transformOriginChanged, this, changed)
               << connect(item, &QQuickItem::parentChanged, this, rebuilt)
               << connect(item, &QObject::destroyed, this, rebuilt);
}

void TransformWatcher::rebuild() {
  for (const QMetaObject::Connection& connection : std::as_const(connections_)) {
    disconnect(connection);
  }
  connections_.clear();
  if (!a_ || !b_) return;

  // The nearest common ancestor: the first item on b's chain (b included) that is on a's chain.
  // The transform between a and b depends on the items below it only.
  QSet<const QQuickItem*> aChain;
  for (QQuickItem* item = a_; item; item = item->parentItem()) aChain.insert(item);
  QQuickItem* common = nullptr;
  for (QQuickItem* item = b_; item; item = item->parentItem()) {
    if (aChain.contains(item)) {
      common = item;
      break;
    }
  }
  for (QQuickItem* item = a_; item && item != common; item = item->parentItem()) listen(item);
  for (QQuickItem* item = b_; item && item != common; item = item->parentItem()) listen(item);
}

}  // namespace wabar::host

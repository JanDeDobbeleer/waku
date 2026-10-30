#pragma once

#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQuickItem>

namespace wabar::host {

// Quickshell's `TransformWatcher`: tells a binding when the transform between two items changed.
//
//     TransformWatcher { id: watcher; a: button; b: anchor }
//     x: { watcher.transform; return button.mapToItem(anchor, 0, 0).x }
//
//   a, b          the two items
//   commonParent  an item known to be an ancestor of both. It is only a hint in Quickshell; here
//                 the nearest common ancestor is always found, so a wrong hint does no harm
//   transform     read-only. Its value means nothing; `transformChanged()` fires whenever the
//                 position, size, scale, rotation or parent of an item on the path from `a` to `b`
//                 changes, so a binding that reads it re-evaluates
//
// Threading: GUI thread only. It listens to the items on the path (below the common ancestor) and
// rebuilds the path when an item is reparented or destroyed.
class TransformWatcher : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QQuickItem* a READ a WRITE setA NOTIFY aChanged FINAL)
  Q_PROPERTY(QQuickItem* b READ b WRITE setB NOTIFY bChanged FINAL)
  Q_PROPERTY(QQuickItem* commonParent READ commonParent WRITE setCommonParent NOTIFY
                 commonParentChanged FINAL)
  Q_PROPERTY(QObject* transform READ transform NOTIFY transformChanged FINAL)

 public:
  explicit TransformWatcher(QObject* parent = nullptr) : QObject(parent) {}

  QQuickItem* a() const { return a_; }
  QQuickItem* b() const { return b_; }
  QQuickItem* commonParent() const { return commonParent_; }
  QObject* transform() { return &token_; }

  void setA(QQuickItem* item);
  void setB(QQuickItem* item);
  void setCommonParent(QQuickItem* item);

 signals:
  void aChanged();
  void bChanged();
  void commonParentChanged();
  void transformChanged();

 private:
  void rebuild();
  void listen(QQuickItem* item);

  QPointer<QQuickItem> a_;
  QPointer<QQuickItem> b_;
  QPointer<QQuickItem> commonParent_;
  QObject token_;
  QList<QMetaObject::Connection> connections_;
};

}  // namespace wabar::host

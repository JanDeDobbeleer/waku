#pragma once

#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QObject>
#include <QQmlListProperty>

namespace wabar::host {

// A group of shell objects (Quickshell `Scope`): windows, Variants, LazyLoaders and non-visual
// parts, kept alive as its children. It draws nothing.
//
// A "part" is any object that is not a window, such as the non-visual WallpaperSettings. The host
// activates the parts of the root and of nested Scopes once the shell is installed: it calls their
// `activate()` (a Q_INVOKABLE, found by name so that wabar does not depend on the part's module).
// A part is deactivated by being destroyed with the shell.
class Scope : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_CLASSINFO("DefaultProperty", "data")
  Q_PROPERTY(QQmlListProperty<QObject> data READ data FINAL)

 public:
  explicit Scope(QObject* parent = nullptr) : QObject(parent) {}

  QQmlListProperty<QObject> data() {
    return QQmlListProperty<QObject>(this, this, &Scope::appendData, &Scope::dataCount,
                                     &Scope::dataAt, &Scope::clearData);
  }
  // What was declared in it, in order.
  const QList<QObject*>& children() const { return data_; }

 private:
  static void appendData(QQmlListProperty<QObject>* list, QObject* object) {
    auto* self = static_cast<Scope*>(list->data);
    if (!object) return;
    self->data_.append(object);
    if (!object->parent()) object->setParent(self);
  }
  static qsizetype dataCount(QQmlListProperty<QObject>* list) {
    return static_cast<Scope*>(list->data)->data_.size();
  }
  static QObject* dataAt(QQmlListProperty<QObject>* list, qsizetype index) {
    return static_cast<Scope*>(list->data)->data_.at(index);
  }
  static void clearData(QQmlListProperty<QObject>* list) {
    static_cast<Scope*>(list->data)->data_.clear();
  }

  QList<QObject*> data_;
};

// The root of shell.qml (Quickshell `ShellRoot`): a Scope the host builds and replaces as a whole.
//
//     import wabar
//     ShellRoot {
//         PanelWindow { anchors { top: true; left: true; right: true }; implicitHeight: 30 }
//     }
class ShellRoot : public Scope {
  Q_OBJECT
  QML_ELEMENT

 public:
  explicit ShellRoot(QObject* parent = nullptr) : Scope(parent) {}
};

}  // namespace wabar::host

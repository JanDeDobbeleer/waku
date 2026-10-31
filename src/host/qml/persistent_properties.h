#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QList>
#include <QMetaProperty>
#include <QObject>
#include <QQmlParserStatus>
#include <QString>
#include <QVariantMap>
#include <optional>

class QQmlEngine;

namespace wabar::host {

// Quickshell's `PersistentProperties`: properties that survive a reload of the shell.
//
//     PersistentProperties {
//         id: state
//         reloadableId: "panelState"
//         property bool expanded: false
//     }
//
// The properties are the ones the QML declares on the object (`property bool expanded`); the type
// has no others of its own besides `reloadableId`. Each object writes its values to a store
// whenever one changes. When an object completes, the stored values of its `reloadableId` (left by
// the object of the shell being replaced, or by one a LazyLoader destroyed) are copied into the
// properties of the same name. Properties that hold objects are not kept. An empty `reloadableId`
// keeps nothing.
//
//   loaded()    after every completion, also when nothing was restored
//   reloaded()  after values were restored from an earlier object
//
// The store is per QML engine. Threading: GUI thread only.
class PersistentProperties : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_PROPERTY(
      QString reloadableId READ reloadableId WRITE setReloadableId NOTIFY reloadableIdChanged FINAL)

 public:
  explicit PersistentProperties(QObject* parent = nullptr);

  QString reloadableId() const { return reloadableId_; }
  void setReloadableId(const QString& id);

  void classBegin() override {}
  void componentComplete() override;

  // The declared (dynamic) properties and their values; objects are left out.
  QVariantMap snapshot() const;

 signals:
  void reloadableIdChanged();
  void loaded();
  void reloaded();

 private slots:
  // Writes the current values to the store (connected to the change signals).
  void save();

 private:
  void restore(const QVariantMap& values);
  // The property's value as the store keeps it; nothing for an object or a JS function.
  std::optional<QVariant> plainValue(const QMetaProperty& property) const;

  QString reloadableId_;
  bool complete_ = false;
  const QQmlEngine* engine_ = nullptr;
  // Change signal (method index) to the declared properties it notifies.
  QHash<int, QList<int>> notifiedProperties_;
};

}  // namespace wabar::host

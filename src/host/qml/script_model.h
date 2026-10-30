#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>

namespace wabar::host {

// Quickshell's `ScriptModel`: a list model driven by a JavaScript array. Assigning a new array
// does not reset the model; it inserts, removes and moves rows so a view keeps the delegates of the
// values that stay.
//
//     ListView { model: ScriptModel { values: apps.filter(a => a.name.includes(query)) } }
//
//   values      the array. The values should be unique; with duplicates the order of the rows is
//               still the order of the array, but which duplicate keeps its delegate is undefined
//   objectProp  the property by which objects (JS objects, QObjects) are compared. Empty: a QObject
//               is compared by identity, a JS object by value. Objects with the same property value
//               are the same row: its `modelData` is replaced by the new object
//
// One role, `modelData`, holds the value. Threading: GUI thread only.
class ScriptModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY valuesChanged FINAL)
  Q_PROPERTY(QString objectProp READ objectProp WRITE setObjectProp NOTIFY objectPropChanged FINAL)

 public:
  explicit ScriptModel(QObject* parent = nullptr);

  QVariantList values() const { return values_; }
  void setValues(const QVariantList& values);
  QString objectProp() const { return objectProp_; }
  void setObjectProp(const QString& prop);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 signals:
  void valuesChanged();
  void objectPropChanged();

 private:
  // What identifies `value` among the rows.
  QVariant keyOf(const QVariant& value) const;

  QVariantList values_;
  QString objectProp_;
};

}  // namespace wabar::host

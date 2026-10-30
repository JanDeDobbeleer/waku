#include "host/qml/script_model.h"

#include <QMetaType>
#include <QObject>
#include <QVariantMap>

namespace wabar::host {

namespace {

constexpr int kModelData = Qt::UserRole;

QObject* asObject(const QVariant& value) {
  if (value.metaType().flags().testFlag(QMetaType::PointerToQObject)) {
    return value.value<QObject*>();
  }
  return nullptr;
}

}  // namespace

ScriptModel::ScriptModel(QObject* parent) : QAbstractListModel(parent) {}

int ScriptModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(values_.size());
}

QVariant ScriptModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= values_.size()) return {};
  if (role == kModelData || role == Qt::DisplayRole) return values_.at(index.row());
  return {};
}

QHash<int, QByteArray> ScriptModel::roleNames() const {
  return {{kModelData, QByteArrayLiteral("modelData")}};
}

QVariant ScriptModel::keyOf(const QVariant& value) const {
  if (QObject* object = asObject(value)) {
    if (!objectProp_.isEmpty()) return object->property(objectProp_.toUtf8().constData());
    return QVariant::fromValue(reinterpret_cast<quintptr>(object));
  }
  if (!objectProp_.isEmpty() && value.metaType().id() == QMetaType::QVariantMap) {
    const QVariantMap map = value.toMap();
    const auto it = map.constFind(objectProp_);
    if (it != map.constEnd()) return *it;
  }
  return value;
}

void ScriptModel::setObjectProp(const QString& prop) {
  if (prop == objectProp_) return;
  objectProp_ = prop;
  emit objectPropChanged();
}

void ScriptModel::setValues(const QVariantList& values) {
  // Rows are matched by key: first the rows that are gone are removed (back to front), then each
  // wanted row is kept, moved up from further down, or inserted.
  QVariantList currentKeys;
  currentKeys.reserve(values_.size());
  for (const QVariant& value : std::as_const(values_)) currentKeys.append(keyOf(value));
  QVariantList wantedKeys;
  wantedKeys.reserve(values.size());
  for (const QVariant& value : values) wantedKeys.append(keyOf(value));

  for (qsizetype i = currentKeys.size() - 1; i >= 0; --i) {
    if (wantedKeys.contains(currentKeys.at(i))) continue;
    beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
    values_.removeAt(i);
    currentKeys.removeAt(i);
    endRemoveRows();
  }

  const auto refresh = [&](qsizetype row, const QVariant& value) {
    if (values_.at(row) == value) return;
    values_[row] = value;
    const QModelIndex idx = index(static_cast<int>(row));
    emit dataChanged(idx, idx, {kModelData, Qt::DisplayRole});
  };

  for (qsizetype i = 0; i < values.size(); ++i) {
    const QVariant& key = wantedKeys.at(i);
    if (i < currentKeys.size() && currentKeys.at(i) == key) {
      refresh(i, values.at(i));
      continue;
    }
    const qsizetype from = currentKeys.indexOf(key, i + 1);
    if (from > i) {
      beginMoveRows({}, static_cast<int>(from), static_cast<int>(from), {}, static_cast<int>(i));
      values_.move(from, i);
      currentKeys.move(from, i);
      endMoveRows();
      refresh(i, values.at(i));
    } else {
      beginInsertRows({}, static_cast<int>(i), static_cast<int>(i));
      values_.insert(i, values.at(i));
      currentKeys.insert(i, key);
      endInsertRows();
    }
  }
  // Duplicates in the old list can leave rows behind; cut to the wanted length.
  if (values_.size() > values.size()) {
    beginRemoveRows({}, static_cast<int>(values.size()), static_cast<int>(values_.size()) - 1);
    values_.resize(values.size());
    endRemoveRows();
  }
  emit valuesChanged();
}

}  // namespace wabar::host

#include "host/qml/script_model.h"

#include <QHash>
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

// A hash that agrees with QVariant::operator== for the keys a model usually has: strings, numbers
// (compared across their types, so hashed as a double) and object identities. Every other key
// lands in one bucket and is compared one by one, as before.
size_t keyHash(const QVariant& key) {
  switch (key.metaType().id()) {
    case QMetaType::QString:
      return qHash(*static_cast<const QString*>(key.constData()));
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
      return qHash(key.toDouble());
    default:
      return 0;
  }
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

  // Which keys are still wanted, by hash: a list of N rows took N x M comparisons here.
  QMultiHash<size_t, qsizetype> wantedByHash;
  wantedByHash.reserve(wantedKeys.size());
  for (qsizetype i = 0; i < wantedKeys.size(); ++i)
    wantedByHash.insert(keyHash(wantedKeys.at(i)), i);
  const auto wanted = [&](const QVariant& key) {
    const auto [first, last] = wantedByHash.equal_range(keyHash(key));
    for (auto it = first; it != last; ++it) {
      if (wantedKeys.at(*it) == key) return true;
    }
    return false;
  };
  for (qsizetype i = currentKeys.size() - 1; i >= 0; --i) {
    if (wanted(currentKeys.at(i))) continue;
    beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
    values_.removeAt(i);
    currentKeys.removeAt(i);
    endRemoveRows();
  }

  // The kept rows not yet placed, by hash: a wanted key that is not among them is inserted without
  // scanning the rest of the rows for it.
  QMultiHash<size_t, QVariant> unplaced;
  unplaced.reserve(currentKeys.size());
  for (const QVariant& key : std::as_const(currentKeys)) unplaced.insert(keyHash(key), key);
  const auto take = [&](const QVariant& key) {
    auto [it, last] = unplaced.equal_range(keyHash(key));
    for (; it != last; ++it) {
      if (*it == key) {
        unplaced.erase(it);
        return true;
      }
    }
    return false;
  };

  const auto refresh = [&](qsizetype row, const QVariant& value) {
    if (values_.at(row) == value) return;
    values_[row] = value;
    const QModelIndex idx = index(static_cast<int>(row));
    emit dataChanged(idx, idx, {kModelData, Qt::DisplayRole});
  };

  for (qsizetype i = 0; i < values.size(); ++i) {
    const QVariant& key = wantedKeys.at(i);
    if (i < currentKeys.size() && currentKeys.at(i) == key) {
      take(key);
      refresh(i, values.at(i));
      continue;
    }
    const qsizetype from = take(key) ? currentKeys.indexOf(key, i + 1) : -1;
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

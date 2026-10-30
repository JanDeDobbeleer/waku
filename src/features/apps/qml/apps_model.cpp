#include "features/apps/qml/apps_model.h"

#include "features/apps/qml/desktop_entry.h"

namespace wabar::apps {

AppsModel::AppsModel(QObject* parent) : QAbstractListModel(parent) {}

QVariantList AppsModel::values() const {
  QVariantList list;
  list.reserve(entries_.size());
  for (DesktopEntry* entry : entries_) list.append(QVariant::fromValue(entry));
  return list;
}

void AppsModel::setEntries(QList<DesktopEntry*> entries) {
  if (entries == entries_) return;
  beginResetModel();
  entries_ = std::move(entries);
  endResetModel();
  emit valuesChanged();
}

int AppsModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant AppsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};
  if (role == Qt::UserRole) return QVariant::fromValue(entries_.at(index.row()));
  return {};
}

QHash<int, QByteArray> AppsModel::roleNames() const {
  return {{Qt::UserRole, QByteArrayLiteral("modelData")}};
}

}  // namespace wabar::apps

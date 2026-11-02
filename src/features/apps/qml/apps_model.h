#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QList>
#include <QVariantList>

namespace waku::apps {

class DesktopEntry;

// Quickshell's `DesktopEntries.applications`: a model of DesktopEntry objects (role `modelData`)
// that also offers them as a JS array in `values`, sorted by name. The service fills it.
class AppsModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged FINAL)
  Q_PROPERTY(int count READ count NOTIFY valuesChanged FINAL)

 public:
  explicit AppsModel(QObject* parent = nullptr);

  QVariantList values() const;
  int count() const { return static_cast<int>(entries_.size()); }
  const QList<DesktopEntry*>& entries() const { return entries_; }

  // Replaces the contents; emits valuesChanged once.
  void setEntries(QList<DesktopEntry*> entries);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 signals:
  void valuesChanged();

 private:
  QList<DesktopEntry*> entries_;
};

}  // namespace waku::apps

#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QString>
#include <memory>
#include <mutex>
#include <vector>

#include "features/tray/tray_types.h"

namespace waku::tray {

// The scaled image of every row by id, for TrayImageProvider. The model writes it on the GUI
// thread; the provider reads it on whatever thread Qt calls it (the pixmap reader thread when the
// Image is asynchronous). Shared, so a provider that outlives the model reads an empty store.
class TrayImageStore {
 public:
  void set(int id, const QImage& image) {
    std::lock_guard lock(mutex_);
    if (image.isNull()) {
      images_.remove(id);
    } else {
      images_.insert(id, image);
    }
  }
  void remove(int id) {
    std::lock_guard lock(mutex_);
    images_.remove(id);
  }
  void clear() {
    std::lock_guard lock(mutex_);
    images_.clear();
  }
  // Null when unknown. QImage is implicitly shared, so the copy is cheap and safe to hand over.
  QImage image(int id) const {
    std::lock_guard lock(mutex_);
    return images_.value(id);
  }

 private:
  mutable std::mutex mutex_;
  QHash<int, QImage> images_;
};

// The tray icons on screen as a list model, rows in arrival order. Holds a copy of each icon's
// data and its scaled image. Not created from QML (Systray owns it). GUI thread only.
//
// `id` is the TrayIconRegistry id (unique within a run, never reused); `key` is the pin-state key
// (iconStateKey: guid, else exe path) and can repeat across icons of one program, so addressing
// an icon always goes by `id`.
class TrayIconModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)

 public:
  enum Role {
    IdRole = Qt::UserRole + 1,
    KeyRole,
    TooltipRole,
    ExeRole,
    IconUrlRole,  // image://tray/<id>/<revision>
    HwndRole,     // as a string: QML numbers cannot hold a 64-bit handle
    HasGuidRole,
    HiddenRole,  // NIS_HIDDEN: the program asked for the icon not to be shown
  };

  explicit TrayIconModel(QObject* parent = nullptr);
  ~TrayIconModel() override;

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Adds the icon (appended) or, when `id` is already a row, replaces its data. The image is
  // rebuilt (scaleTrayIcon of the data's RGBA) and the revision bumped only when the pixels
  // differ from the previous ones. Emits dataChanged for that one row.
  void apply(int id, const TrayIconData& data);
  // Removes the row of `id`; false when there is none.
  bool remove(int id);

  int rowOfId(int id) const;
  // Null for an unknown id.
  const TrayIconData* iconData(int id) const;
  // The scaled image of `id`, null image when unknown.
  QImage image(int id) const;
  int revision(int id) const;
  // The images for the provider; any thread may read it.
  std::shared_ptr<const TrayImageStore> imageStore() const { return store_; }

 signals:
  void countChanged();

 private:
  struct Row {
    int id = -1;
    TrayIconData data;
    QImage image;
    int revision = 0;
  };
  std::vector<Row> rows_;
  std::shared_ptr<TrayImageStore> store_ = std::make_shared<TrayImageStore>();
};

}  // namespace waku::tray

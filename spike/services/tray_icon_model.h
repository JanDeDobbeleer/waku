#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QImage>
#include <QString>
#include <vector>

#include "platform/systray_types.h"

namespace wabar::services {

namespace platform = ::yasb::platform;

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

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Adds the icon (appended) or, when `id` is already a row, replaces its data. The image is
  // rebuilt (scaleTrayIcon of the data's RGBA) and the revision bumped only when the pixels
  // differ from the previous ones. Emits dataChanged for that one row.
  void apply(int id, const platform::TrayIconData& data);
  // Removes the row of `id`; false when there is none.
  bool remove(int id);

  int rowOfId(int id) const;
  // Null for an unknown id.
  const platform::TrayIconData* iconData(int id) const;
  // The scaled image of `id`, null image when unknown.
  QImage image(int id) const;
  int revision(int id) const;

 signals:
  void countChanged();

 private:
  struct Row {
    int id = -1;
    platform::TrayIconData data;
    QImage image;
    int revision = 0;
  };
  std::vector<Row> rows_;
};

}  // namespace wabar::services

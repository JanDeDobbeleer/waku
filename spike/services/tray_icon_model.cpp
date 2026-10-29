#include "spike/services/tray_icon_model.h"

#include <algorithm>
#include <cstring>

#include "widgets/systray/icon_scale.h"
#include "widgets/systray/systray_state.h"

namespace wabar::services {

namespace {

QImage buildImage(const platform::TrayIconData& data) {
  if (data.iconWidth <= 0 || data.iconHeight <= 0 || data.iconRgba.isEmpty()) return {};
  const qsizetype rowBytes = qsizetype{4} * data.iconWidth;
  if (data.iconRgba.size() < rowBytes * data.iconHeight) return {};
  QImage source(data.iconWidth, data.iconHeight, QImage::Format_RGBA8888);
  if (source.isNull()) return {};
  for (int y = 0; y < data.iconHeight; ++y) {
    std::memcpy(source.scanLine(y), data.iconRgba.constData() + y * rowBytes,
                static_cast<size_t>(rowBytes));
  }
  return yasb::widgets::scaleTrayIcon(source);
}

}  // namespace

TrayIconModel::TrayIconModel(QObject* parent) : QAbstractListModel(parent) {}

int TrayIconModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TrayIconModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
    return {};
  }
  const Row& row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case IdRole:
      return row.id;
    case KeyRole:
      return yasb::widgets::iconStateKey(row.data);
    case TooltipRole:
      return row.data.szTip;
    case ExeRole:
      return row.data.exe;
    case IconUrlRole:
      if (row.image.isNull()) return QString();
      return QStringLiteral("image://tray/%1/%2").arg(row.id).arg(row.revision);
    case HwndRole:
      return QString::number(row.data.hWnd);
    case HasGuidRole:
      return row.data.guid.has_value();
    case HiddenRole:
      return (row.data.uFlags & platform::kNifState) != 0 && row.data.dwState == 1;
    default:
      return {};
  }
}

QHash<int, QByteArray> TrayIconModel::roleNames() const {
  return {
      {IdRole, "id"},           {KeyRole, "key"},         {TooltipRole, "tooltip"},
      {ExeRole, "exe"},         {IconUrlRole, "iconUrl"}, {HwndRole, "hwnd"},
      {HasGuidRole, "hasGuid"}, {HiddenRole, "hidden"},
  };
}

void TrayIconModel::apply(int id, const platform::TrayIconData& data) {
  const int at = rowOfId(id);
  if (at < 0) {
    Row row;
    row.id = id;
    row.data = data;
    row.image = buildImage(data);
    row.revision = row.image.isNull() ? 0 : 1;
    const int pos = static_cast<int>(rows_.size());
    beginInsertRows({}, pos, pos);
    rows_.push_back(std::move(row));
    endInsertRows();
    emit countChanged();
    return;
  }
  Row& row = rows_[static_cast<size_t>(at)];
  const bool pixelsChanged = row.data.iconWidth != data.iconWidth ||
                             row.data.iconHeight != data.iconHeight ||
                             row.data.iconRgba != data.iconRgba;
  row.data = data;
  if (pixelsChanged) {
    row.image = buildImage(data);
    ++row.revision;
  }
  const QModelIndex idx = index(at);
  emit dataChanged(idx, idx);
}

bool TrayIconModel::remove(int id) {
  const int at = rowOfId(id);
  if (at < 0) return false;
  beginRemoveRows({}, at, at);
  rows_.erase(rows_.begin() + at);
  endRemoveRows();
  emit countChanged();
  return true;
}

int TrayIconModel::rowOfId(int id) const {
  const auto it =
      std::find_if(rows_.begin(), rows_.end(), [id](const Row& r) { return r.id == id; });
  return it == rows_.end() ? -1 : static_cast<int>(it - rows_.begin());
}

const platform::TrayIconData* TrayIconModel::iconData(int id) const {
  const int at = rowOfId(id);
  return at < 0 ? nullptr : &rows_[static_cast<size_t>(at)].data;
}

QImage TrayIconModel::image(int id) const {
  const int at = rowOfId(id);
  return at < 0 ? QImage() : rows_[static_cast<size_t>(at)].image;
}

int TrayIconModel::revision(int id) const {
  const int at = rowOfId(id);
  return at < 0 ? 0 : rows_[static_cast<size_t>(at)].revision;
}

}  // namespace wabar::services

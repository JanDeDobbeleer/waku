#pragma once

#include <QDir>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

#include "platform/systray_types.h"

namespace yasb::widgets {

// Port of IconState (services/systray/systray_widget.py).
struct IconState {
  bool isPinned = false;
  int index = 0;
  bool operator==(const IconState& other) const {
    return isPinned == other.isPinned && index == other.index;
  }
};

// A Python dict of key -> IconState: lookups by key, iteration in insertion order (a re-assigned
// key keeps its position).
class IconStateMap {
 public:
  using Entry = std::pair<QString, IconState>;

  const IconState* find(const QString& key) const;
  void set(const QString& key, const IconState& state);
  bool empty() const { return entries_.empty(); }
  size_t size() const { return entries_.size(); }
  const std::vector<Entry>& entries() const { return entries_; }
  void clear() { entries_.clear(); }

 private:
  std::vector<Entry> entries_;
};

// Key an icon is saved under: str(guid) when it has one, else the executable path.
QString iconStateKey(const platform::TrayIconData& data);

// SystrayWidget.update_current_state: renumbers the saved order so the icons currently on screen
// (in screen order) take the slots of the saved ones. `pinned` and `unpinned` are the visible keys
// of each container, in layout order, without duplicates.
void updateCurrentState(IconStateMap& state, const QStringList& pinned,
                        const QStringList& unpinned);

// SystrayWidget.update_icon_data: copies the fields of a tray notification into the stored icon
// data, honouring the NIF_* flags.
void updateIconData(platform::TrayIconData& old, const platform::TrayIconData& incoming);

// %LOCALAPPDATA%\YASB, created on demand (core/utils/system.py app_data_path).
QString appDataDir();

// "systray_state_<screen id>.json" in `dir`, in Python YASB's format: a JSON object of key ->
// {"is_pinned": bool, "index": int}, indent 2, non-ASCII escaped. Saving merges into whatever the
// file already holds so other screens' and other builds' keys survive.
class SystrayStateStore {
 public:
  // `screenId` is the cleaned screen identity ("None" when there is no screen).
  SystrayStateStore(QString dir, QString screenId)
      : dir_(std::move(dir)), screenId_(std::move(screenId)) {}

  QString path() const;
  IconStateMap load() const;
  // Returns false when the file could not be written.
  bool save(const IconStateMap& state) const;

 private:
  QString dir_;
  QString screenId_;
};

// QScreen identity as Python builds it: upper(manufacturer + name + serial), non-word characters
// removed.
QString cleanScreenId(const QString& manufacturer, const QString& name, const QString& serial);

}  // namespace yasb::widgets

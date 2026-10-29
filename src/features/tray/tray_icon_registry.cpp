#include "features/tray/tray_icon_registry.h"

#include <algorithm>

#include "features/tray/tray_state.h"

namespace wabar::tray {

void TrayIconRegistry::setFilter(QSet<QUuid> guids, const QStringList& hiddenExeNames) {
  filteredGuids_ = std::move(guids);
  hiddenExesLower_.clear();
  for (const QString& name : hiddenExeNames) hiddenExesLower_ << name.toLower();
}

bool TrayIconRegistry::isFiltered(const TrayIconData& data) const {
  if (data.guid && filteredGuids_.contains(*data.guid)) return true;
  return !hiddenExesLower_.isEmpty() && hiddenExesLower_.contains(data.exe.toLower());
}

TrayIconRegistry::Result TrayIconRegistry::modified(const TrayIconData& incoming) {
  if (isFiltered(incoming)) return {};
  if (Entry* known = findEntry(incoming.guid, incoming.hWnd, incoming.uID)) {
    updateIconData(known->data, incoming);
    return {Change::Updated, known->id};
  }
  Entry entry;
  entry.id = nextId_++;
  updateIconData(entry.data, incoming);
  entries_.push_back(std::move(entry));
  return {Change::Added, entries_.back().id};
}

std::optional<TrayIconRegistry::Id> TrayIconRegistry::deleted(const TrayIconData& incoming) {
  const Entry* known = findEntry(incoming.guid, incoming.hWnd, incoming.uID);
  if (!known) return std::nullopt;
  const Id id = known->id;
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [id](const Entry& entry) { return entry.id == id; }),
                 entries_.end());
  return id;
}

std::vector<TrayIconRegistry::Id> TrayIconRegistry::sweepStale(TrayWindowApi& api) {
  std::vector<Id> removed;
  for (const Entry& entry : entries_) {
    if (!api.isWindow(entry.data.hWnd)) removed.push_back(entry.id);
  }
  if (!removed.empty()) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&removed](const Entry& entry) {
                                    return std::find(removed.begin(), removed.end(), entry.id) !=
                                           removed.end();
                                  }),
                   entries_.end());
  }
  return removed;
}

std::optional<TrayIconRegistry::Id> TrayIconRegistry::find(const std::optional<QUuid>& guid,
                                                           std::uint64_t hwnd,
                                                           std::uint32_t uid) const {
  const Entry* entry = findEntry(guid, hwnd, uid);
  return entry ? std::optional<Id>(entry->id) : std::nullopt;
}

const TrayIconData* TrayIconRegistry::data(Id id) const {
  for (const Entry& entry : entries_) {
    if (entry.id == id) return &entry.data;
  }
  return nullptr;
}

std::vector<TrayIconRegistry::Id> TrayIconRegistry::ids() const {
  std::vector<Id> ids;
  ids.reserve(entries_.size());
  for (const Entry& entry : entries_) ids.push_back(entry.id);
  return ids;
}

const TrayIconRegistry::Entry* TrayIconRegistry::findEntry(const std::optional<QUuid>& guid,
                                                           std::uint64_t hwnd,
                                                           std::uint32_t uid) const {
  if (guid) {
    for (const Entry& entry : entries_) {
      if (entry.data.guid && *entry.data.guid == *guid) return &entry;
    }
  }
  for (const Entry& entry : entries_) {
    if (entry.data.hWnd == hwnd && entry.data.uID == uid) return &entry;
  }
  return nullptr;
}

TrayIconRegistry::Entry* TrayIconRegistry::findEntry(const std::optional<QUuid>& guid,
                                                     std::uint64_t hwnd, std::uint32_t uid) {
  return const_cast<Entry*>(std::as_const(*this).findEntry(guid, hwnd, uid));
}

}  // namespace wabar::tray

#pragma once

#include <QSet>
#include <QStringList>
#include <QUuid>
#include <cstdint>
#include <optional>
#include <vector>

#include "platform/systray_host.h"
#include "platform/systray_types.h"

namespace yasb::widgets {

// The set of tray icons currently announced, without any UI: which notification belongs to which
// icon, which ones are filtered out, and which owners have gone away. A systray widget (Widgets or
// tidy) keeps one view object per icon and maps it by id.
//
// Ports of the icon lifecycle in SystrayWidget: findIcon, the filtered-guid and hide_icons checks
// of onIconModified, onIconDeleted and checkIcons.
//
// Identity: an icon with a guid is found by that guid; failing that (and for icons without one) by
// owner window and icon id. Ids are small integers handed out in arrival order and never reused
// within one registry, so a view can hold one without it ever meaning another icon.
//
// Threads: none of its own; use it from the thread that applies the host's notifications (the
// GUI thread).
class TrayIconRegistry {
 public:
  using Id = int;

  enum class Change { Ignored, Added, Updated };
  struct Result {
    Change change = Change::Ignored;
    Id id = -1;  // valid unless Ignored
  };

  // Icons with these guids (Windows' own battery, volume and network icons) and icons of these
  // executables (names compared without regard to case) are never admitted.
  void setFilter(QSet<QUuid> guids, const QStringList& hiddenExeNames);
  bool isFiltered(const platform::TrayIconData& data) const;

  // A NIM_ADD or NIM_MODIFY notification. Filtered icons are ignored; an icon already known has
  // the fields named by the notification's NIF_* flags copied in (updateIconData); a new one gets
  // an id and starts from the same copy of an empty record.
  Result modified(const platform::TrayIconData& incoming);

  // A NIM_DELETE notification: the id of the icon removed, none when it was not known. The filter
  // does not apply (an icon that was never admitted is simply unknown).
  std::optional<Id> deleted(const platform::TrayIconData& incoming);

  // Removes every icon whose owner window no longer exists and returns their ids in arrival order.
  std::vector<Id> sweepStale(platform::TrayWindowApi& api);

  std::optional<Id> find(const std::optional<QUuid>& guid, std::uint64_t hwnd,
                         std::uint32_t uid) const;
  // The accumulated data of an icon; null for an unknown id.
  const platform::TrayIconData* data(Id id) const;
  // Every id, in arrival order.
  std::vector<Id> ids() const;
  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    Id id = -1;
    platform::TrayIconData data;
  };

  Entry* findEntry(const std::optional<QUuid>& guid, std::uint64_t hwnd, std::uint32_t uid);
  const Entry* findEntry(const std::optional<QUuid>& guid, std::uint64_t hwnd,
                         std::uint32_t uid) const;

  QSet<QUuid> filteredGuids_;
  QStringList hiddenExesLower_;
  std::vector<Entry> entries_;
  Id nextId_ = 0;
};

}  // namespace yasb::widgets

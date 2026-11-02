#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

#include "features/github/github_api.h"
#include "features/github/github_icons.h"

// Pure helpers of yasb:src/core/widgets/yasb/github.py and core/utils/time_utils.py.
namespace waku::github {

// time_utils.get_relative_time(iso, short=False); "" for an empty or unparsable timestamp.
QString relativeTime(const QString& isoTimestamp, const QDateTime& nowUtc);

// GithubWidget._format_category_title.
QString categoryTitle(const QString& categoryType);

struct IconAndStates {
  QString icon;
  QStringList states;
};
// GithubWidget._resolve_icon_and_states.
IconAndStates iconAndStates(const GithubNotification& notification, const GithubIcons& icons);

// Python `text[: max - 3] + "..."` when len(text) > max, counted in code points.
QString truncateField(const QString& text, int maxSize);

// Notification types in configured order (case-insensitive), then the rest in first-seen order.
QStringList orderedCategories(const std::vector<GithubNotification>& notifications,
                              const std::vector<std::string>& configuredOrder);

}  // namespace waku::github

#include "widgets/github/github_format.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace yasb::widgets::github {
namespace {

QString plural(long long value, const char* unit) {
  return QStringLiteral("%1 %2%3 ago")
      .arg(value)
      .arg(QLatin1String(unit))
      .arg(value != 1 ? QStringLiteral("s") : QString());
}

QString icon(const std::string& value) { return QString::fromStdString(value); }

}  // namespace

QString relativeTime(const QString& isoTimestamp, const QDateTime& nowUtc) {
  if (isoTimestamp.isEmpty()) return {};
  const QDateTime updated = QDateTime::fromString(isoTimestamp, Qt::ISODateWithMs);
  if (!updated.isValid()) return {};
  const double seconds = static_cast<double>(updated.msecsTo(nowUtc)) / 1000.0;
  const double minutes = seconds / 60;
  const double hours = minutes / 60;
  const double days = hours / 24;
  const double weeks = days / 7;
  const double months = days / 30;
  const double years = days / 365;
  if (seconds < 60) return QStringLiteral("just now");
  if (minutes < 60) return plural(static_cast<long long>(minutes), "minute");
  if (hours < 24) return plural(static_cast<long long>(hours), "hour");
  if (days < 7) return plural(static_cast<long long>(days), "day");
  if (weeks < 4) return plural(static_cast<long long>(weeks), "week");
  if (months < 12) return plural(static_cast<long long>(months), "month");
  return plural(static_cast<long long>(years), "year");
}

QString categoryTitle(const QString& categoryType) {
  static const QHash<QString, QString> custom = {
      {QStringLiteral("Issue"), QStringLiteral("Issues")},
      {QStringLiteral("PullRequest"), QStringLiteral("Pull Requests")},
      {QStringLiteral("Release"), QStringLiteral("Releases")},
      {QStringLiteral("Discussion"), QStringLiteral("Discussions")},
      {QStringLiteral("CheckSuite"), QStringLiteral("Check Suites")},
  };
  if (const auto it = custom.find(categoryType); it != custom.end()) return it.value();
  static const QRegularExpression upper(QStringLiteral("(?<!^)(?=[A-Z])"));
  QString spaced = categoryType;
  spaced.replace(upper, QStringLiteral(" "));
  spaced = spaced.trimmed();
  return spaced.isEmpty() ? categoryType : spaced;
}

IconAndStates iconAndStates(const platform::GithubNotification& n, const GithubIcons& icons) {
  IconAndStates result;
  if (n.type == QLatin1String("Issue")) {
    const QString state = n.issueState.value_or(QString()).toLower();
    result.icon = icon(state == QLatin1String("closed") ? icons.issue_closed : icons.issue);
    if (!state.isEmpty()) result.states << state;
  } else if (n.type == QLatin1String("PullRequest")) {
    const QString state = n.pullRequestState.value_or(QString()).toLower();
    if (n.pullRequestIsMerged) {
      result.icon = icon(icons.pull_request_merged);
      result.states << QStringLiteral("merged");
    } else if (state == QLatin1String("closed")) {
      result.icon = icon(icons.pull_request_closed);
    } else if (n.pullRequestIsDraft.value_or(false)) {
      result.icon = icon(icons.pull_request_draft);
      result.states << QStringLiteral("draft");
    } else {
      result.icon = icon(icons.pull_request);
    }
    if (!state.isEmpty()) result.states << state;
  } else if (n.type == QLatin1String("Discussion")) {
    if (n.discussionIsAnswered.value_or(false)) {
      result.icon = icon(icons.discussion_answered);
      result.states << QStringLiteral("answered");
    } else {
      result.icon = icon(icons.discussion);
    }
  } else if (n.type == QLatin1String("Release")) {
    result.icon = icon(icons.release);
  } else if (n.type == QLatin1String("CheckSuite")) {
    result.icon = icon(icons.checksuite);
  } else {
    result.icon = icon(icons.default_);
  }
  return result;
}

QString truncateField(const QString& text, int maxSize) {
  const QList<uint> codePoints = text.toUcs4();
  const auto length = static_cast<qsizetype>(codePoints.size());
  if (length <= maxSize) return text;
  qsizetype keep = static_cast<qsizetype>(maxSize) - 3;
  if (keep < 0) keep = std::max<qsizetype>(0, length + keep);
  const QList<uint> head = codePoints.mid(0, keep);
  return QString::fromUcs4(reinterpret_cast<const char32_t*>(head.constData()), head.size()) +
         QStringLiteral("...");
}

QStringList orderedCategories(const std::vector<platform::GithubNotification>& notifications,
                              const std::vector<std::string>& configuredOrder) {
  QStringList seen;
  for (const auto& n : notifications) {
    if (!seen.contains(n.type)) seen << n.type;
  }
  // Python's {key.lower(): key} keeps the last key per lower-cased name.
  QHash<QString, QString> lookup;
  for (const QString& category : seen) lookup.insert(category.toLower(), category);
  QStringList ordered;
  for (const std::string& configured : configuredOrder) {
    const auto it = lookup.constFind(QString::fromStdString(configured).toLower());
    if (it != lookup.cend() && !ordered.contains(it.value())) ordered << it.value();
  }
  for (const QString& category : seen) {
    if (!ordered.contains(category)) ordered << category;
  }
  return ordered;
}

}  // namespace yasb::widgets::github

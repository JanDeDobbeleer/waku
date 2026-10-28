#include "spike/bar/screen_match.h"

namespace tidy {

namespace {

bool nameMatches(const QString& screenName, const QString& pattern) {
  if (screenName.compare(pattern, Qt::CaseInsensitive) == 0) return true;
  return screenName.endsWith(QLatin1Char('\\') + pattern, Qt::CaseInsensitive);
}

}  // namespace

std::vector<int> matchScreens(const QStringList& patterns, const std::vector<ScreenRef>& screens) {
  std::vector<int> selected;
  for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
    const ScreenRef& screen = screens[static_cast<size_t>(i)];
    for (const QString& raw : patterns) {
      const QString pattern = raw.trimmed();
      if (pattern.isEmpty()) continue;
      const bool match = pattern.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0 ||
                         (screen.primary &&
                          pattern.compare(QStringLiteral("primary"), Qt::CaseInsensitive) == 0) ||
                         nameMatches(screen.name, pattern);
      if (match) {
        selected.push_back(i);
        break;
      }
    }
  }
  return selected;
}

}  // namespace tidy

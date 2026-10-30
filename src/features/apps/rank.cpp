#include "features/apps/rank.h"

#include <algorithm>

namespace wabar::apps {
namespace {

bool isWordChar(QChar c) { return c.isLetterOrNumber(); }

// Start offsets of the words of `text` (a word starts after a non-alphanumeric character).
std::vector<qsizetype> wordStarts(const QString& text) {
  std::vector<qsizetype> starts;
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (isWordChar(text[i]) && (i == 0 || !isWordChar(text[i - 1]))) starts.push_back(i);
  }
  return starts;
}

// Index of the first word whose start begins `query`, -1 when none.
int wordIndexStartingWith(const QString& text, const QString& query) {
  const auto starts = wordStarts(text);
  for (size_t i = 0; i < starts.size(); ++i) {
    if (text.mid(starts[i]).startsWith(query)) return static_cast<int>(i);
  }
  return -1;
}

int subsequenceScore(const QString& query, const QString& text) {
  qsizetype from = 0;
  int consecutive = 0;
  qsizetype previous = -2;
  for (const QChar c : query) {
    if (c.isSpace()) continue;
    const qsizetype at = text.indexOf(c, from);
    if (at < 0) return 0;
    if (at == previous + 1) ++consecutive;
    previous = at;
    from = at + 1;
  }
  const int letters = static_cast<int>(query.size());
  return 200 + (letters > 1 ? consecutive * 100 / (letters - 1) : 100);
}

}  // namespace

int matchScore(const QString& queryIn, const QString& textIn) {
  const QString query = queryIn.trimmed().toLower();
  if (query.isEmpty()) return 1;
  const QString text = textIn.trimmed().toLower();
  if (text.isEmpty()) return 0;

  if (text == query) return 1000;
  if (text.startsWith(query)) {
    return 800 + static_cast<int>(query.size() * 100 / text.size());
  }
  const int word = wordIndexStartingWith(text, query);
  if (word >= 0) return 700 - std::min(word, 10) * 10;

  const QStringList words = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (words.size() > 1) {
    bool all = true;
    for (const QString& part : words) {
      if (text.startsWith(part)) continue;
      if (wordIndexStartingWith(text, part) < 0) {
        all = false;
        break;
      }
    }
    if (all) return 500;
  }
  return subsequenceScore(query, text);
}

std::vector<int> rankApps(const QString& query, const std::vector<RankCandidate>& candidates,
                          int limit) {
  struct Scored {
    int index;
    int score;
  };
  std::vector<Scored> scored;
  const bool blank = query.trimmed().isEmpty();
  for (size_t i = 0; i < candidates.size(); ++i) {
    int score = 1;
    if (!blank) {
      score = matchScore(query, candidates[i].name);
      for (const QString& extra : candidates[i].extras)
        score = std::max(score, matchScore(query, extra) / 2);
      if (score == 0) continue;
    }
    scored.push_back({static_cast<int>(i), score});
  }
  std::stable_sort(scored.begin(), scored.end(), [&](const Scored& a, const Scored& b) {
    if (a.score != b.score) return a.score > b.score;
    const RankCandidate& left = candidates[static_cast<size_t>(a.index)];
    const RankCandidate& right = candidates[static_cast<size_t>(b.index)];
    if (left.frecency != right.frecency) return left.frecency > right.frecency;
    return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
  });
  std::vector<int> result;
  for (const Scored& item : scored) {
    if (limit > 0 && static_cast<int>(result.size()) >= limit) break;
    result.push_back(item.index);
  }
  return result;
}

}  // namespace wabar::apps

#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace waku::apps {

// How well `query` matches `text`, 0 for no match. Case-insensitive. Tiers, best first:
//   exact            1000
//   prefix            800..900  (shorter texts score higher)
//   word start        600..700  (the query starts a word; earlier words score higher)
//   all words start   500       (every space-separated query word starts some word, any order)
//   subsequence       200..300  (the characters appear in order; consecutive runs score higher)
// A blank query matches everything with 1.
int matchScore(const QString& query, const QString& text);

// One app as the ranking sees it: the name, other searchable text (generic name, keywords, the
// executable) and the frecency score used to break ties.
struct RankCandidate {
  QString name;
  QStringList extras;
  double frecency = 0;
};

// Indexes into `candidates`, best first, at most `limit` (all when limit <= 0). Order: match score
// (a name match beats an extras match of the same tier), then frecency, then name. A blank query
// returns every candidate ordered by frecency, then name.
std::vector<int> rankApps(const QString& query, const std::vector<RankCandidate>& candidates,
                          int limit);

}  // namespace waku::apps

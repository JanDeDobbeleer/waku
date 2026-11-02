#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include "features/apps/frecency.h"
#include "features/apps/rank.h"

using namespace waku::apps;

namespace {

QString s(const char* text) { return QString::fromLatin1(text); }

std::vector<RankCandidate> sample() {
  return {
      {s("Visual Studio Code"), {s("code")}, 0},
      {s("Code Composer"), {}, 0},
      {s("Notepad"), {}, 0},
      {s("Notes"), {}, 0},
      {s("Microsoft Edge"), {s("msedge")}, 0},
      {s("Windows Terminal"), {}, 0},
  };
}

QStringList names(const std::vector<RankCandidate>& list, const std::vector<int>& order) {
  QStringList out;
  for (const int i : order) out.append(list[static_cast<size_t>(i)].name);
  return out;
}

}  // namespace

TEST(AppsRank, TiersAreOrdered) {
  const int exact = matchScore(s("notepad"), s("Notepad"));
  const int prefix = matchScore(s("note"), s("Notepad"));
  const int word = matchScore(s("code"), s("Visual Studio Code"));
  const int allWords = matchScore(s("code visual"), s("Visual Studio Code"));
  const int subsequence = matchScore(s("vsc"), s("Visual Studio Code"));
  EXPECT_EQ(exact, 1000);
  EXPECT_GT(prefix, word);
  EXPECT_GT(exact, prefix);
  EXPECT_GT(word, allWords);
  EXPECT_GT(allWords, subsequence);
  EXPECT_GT(subsequence, 0);
  EXPECT_EQ(matchScore(s("zzz"), s("Notepad")), 0);
}

TEST(AppsRank, PrefixPrefersShorterTexts) {
  EXPECT_GT(matchScore(s("note"), s("Notes")), matchScore(s("note"), s("Notepad")));
}

TEST(AppsRank, WordStartPrefersEarlierWords) {
  EXPECT_GT(matchScore(s("stu"), s("My Studio")), matchScore(s("stu"), s("A B Studio")));
}

TEST(AppsRank, SubsequenceRewardsConsecutiveRuns) {
  EXPECT_GT(matchScore(s("nop"), s("Notepad")), matchScore(s("ntp"), s("Notepad")));
  EXPECT_GE(matchScore(s("ntp"), s("Notepad")), 200);
  EXPECT_LE(matchScore(s("nop"), s("Notepad")), 300);
}

TEST(AppsRank, IsCaseInsensitiveAndTrims) {
  EXPECT_EQ(matchScore(s("  NOTEPAD "), s("notepad")), 1000);
}

TEST(AppsRank, RanksByTierThenName) {
  const auto candidates = sample();
  // Weaker subsequence matches ("Windows Terminal") follow the prefix matches.
  EXPECT_EQ(names(candidates, rankApps(s("note"), candidates, 0)).mid(0, 2),
            (QStringList{s("Notes"), s("Notepad")}));
  // "code": word start in "Visual Studio Code", prefix of "Code Composer"; the prefix wins.
  EXPECT_EQ(names(candidates, rankApps(s("code"), candidates, 0)).mid(0, 2),
            (QStringList{s("Code Composer"), s("Visual Studio Code")}));
}

TEST(AppsRank, ExtrasMatchBelowNames) {
  const auto candidates = sample();
  const auto order = names(candidates, rankApps(s("msedge"), candidates, 0));
  ASSERT_EQ(order.size(), 1);
  EXPECT_EQ(order.first(), s("Microsoft Edge"));
}

TEST(AppsRank, LimitCutsTheList) {
  const auto candidates = sample();
  EXPECT_EQ(rankApps(s("o"), candidates, 2).size(), 2u);
  EXPECT_EQ(rankApps(QString(), candidates, 0).size(), candidates.size());
  EXPECT_TRUE(rankApps(s("qqqq"), candidates, 0).empty());
}

TEST(AppsRank, FrecencyBreaksTies) {
  std::vector<RankCandidate> candidates = {
      {s("Notes"), {}, 0}, {s("Notebook"), {}, 5}, {s("Noted"), {}, 1}};
  // All are prefix matches; scores differ by length, so give equal-length names for a pure tie.
  candidates = {{s("Alpha"), {}, 0}, {s("Alpho"), {}, 5}, {s("Alphu"), {}, 1}};
  EXPECT_EQ(names(candidates, rankApps(s("alph"), candidates, 0)),
            (QStringList{s("Alpho"), s("Alphu"), s("Alpha")}));
  // A better match beats any frecency.
  candidates = {{s("Alphabet Soup"), {}, 100}, {s("Alph"), {}, 0}};
  EXPECT_EQ(names(candidates, rankApps(s("alph"), candidates, 0)).first(), s("Alph"));
}

TEST(AppsRank, BlankQueryOrdersByFrecencyThenName) {
  const std::vector<RankCandidate> candidates = {
      {s("Bravo"), {}, 0}, {s("Alpha"), {}, 0}, {s("Charlie"), {}, 3}};
  EXPECT_EQ(names(candidates, rankApps(QString(), candidates, 0)),
            (QStringList{s("Charlie"), s("Alpha"), s("Bravo")}));
}

TEST(AppsFrecency, CountsAndDecays) {
  FrecencyStore store;
  const qint64 day = 86400000;
  const qint64 t0 = 1000 * day;
  EXPECT_EQ(store.score(s("a"), t0), 0);
  store.record(s("a"), t0);
  store.record(s("a"), t0);
  EXPECT_NEAR(store.score(s("a"), t0), 2.0, 1e-9);
  EXPECT_NEAR(store.score(s("a"), t0 + 14 * day), 1.0, 1e-9);
  // A launch after a while adds to the decayed count.
  store.record(s("a"), t0 + 14 * day);
  EXPECT_NEAR(store.score(s("a"), t0 + 14 * day), 2.0, 1e-9);
  EXPECT_FALSE(store.save());  // no path: memory only
}

TEST(AppsFrecency, SavesAndLoads) {
  QTemporaryDir dir;
  const QString path = dir.filePath(s("apps_frecency.json"));
  {
    FrecencyStore store(path);
    store.record(s("one"), 5000000000);
    store.record(s("two"), 5000000000);
    store.record(s("two"), 5000000000);
    ASSERT_TRUE(store.save());
  }
  FrecencyStore loaded(path);
  loaded.load();
  EXPECT_EQ(loaded.size(), 2);
  EXPECT_NEAR(loaded.score(s("two"), 5000000000), 2.0, 1e-9);
  EXPECT_NEAR(loaded.score(s("one"), 5000000000), 1.0, 1e-9);
}

TEST(AppsFrecency, DamagedFileLeavesTheStoreEmpty) {
  QTemporaryDir dir;
  const QString path = dir.filePath(s("apps_frecency.json"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("{not json");
  file.close();
  FrecencyStore store(path);
  store.load();
  EXPECT_EQ(store.size(), 0);
}

TEST(AppsFrecency, KeepsTheBestEntriesOnSave) {
  QTemporaryDir dir;
  const QString path = dir.filePath(s("apps_frecency.json"));
  FrecencyStore store(path);
  for (int i = 0; i < FrecencyStore::kMaxEntries + 20; ++i)
    store.record(QString::number(i), 1000000000 + i);
  store.record(s("favourite"), 1000000000);
  store.record(s("favourite"), 1000000000);
  store.record(s("favourite"), 1000000000);
  ASSERT_TRUE(store.save());
  FrecencyStore loaded(path);
  loaded.load();
  EXPECT_EQ(loaded.size(), FrecencyStore::kMaxEntries);
  EXPECT_GT(loaded.score(s("favourite"), 1000000000), 2.0);
}

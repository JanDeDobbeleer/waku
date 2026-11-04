#include <gtest/gtest.h>

#include <QVariantMap>

#include "features/tiling/tiling_rules.h"

using namespace waku::tiling;

namespace {

WindowFacts tileable() {
  WindowFacts facts;
  facts.hwnd = 1;
  facts.visible = true;
  facts.thickFrame = true;
  facts.caption = true;
  facts.title = QStringLiteral("Untitled - Notepad");
  facts.className = QStringLiteral("Notepad");
  facts.processName = QStringLiteral("notepad.exe");
  return facts;
}

QVariantMap rule(const QString& process, const QString& action) {
  return {{QStringLiteral("process"), process}, {QStringLiteral("action"), action}};
}

}  // namespace

TEST(TilingRules, AnOrdinaryWindowIsManaged) {
  EXPECT_EQ(classify(tileable(), {}, false).verdict, Verdict::Manage);
}

TEST(TilingRules, EachDisqualifierIgnoresTheWindow) {
  struct Case {
    const char* name;
    void (*apply)(WindowFacts&);
  };
  const Case cases[] = {
      {"invisible", [](WindowFacts& f) { f.visible = false; }},
      {"cloaked", [](WindowFacts& f) { f.cloaked = true; }},
      {"own process", [](WindowFacts& f) { f.ownProcess = true; }},
      {"owned", [](WindowFacts& f) { f.ownedByVisible = true; }},
      {"tool window", [](WindowFacts& f) { f.toolWindow = true; }},
      {"no thick frame", [](WindowFacts& f) { f.thickFrame = false; }},
      {"no caption", [](WindowFacts& f) { f.caption = false; }},
      {"maximized", [](WindowFacts& f) { f.zoomed = true; }},
      {"fullscreen", [](WindowFacts& f) { f.fullscreen = true; }},
      {"splash class", [](WindowFacts& f) { f.className = QStringLiteral("MsoSplash"); }},
      {"elevated", [](WindowFacts& f) { f.elevated = true; }},
  };
  for (const Case& c : cases) {
    WindowFacts facts = tileable();
    c.apply(facts);
    EXPECT_EQ(classify(facts, {}, false).verdict, Verdict::Ignore) << c.name;
  }
}

TEST(TilingRules, AnElevatedWindowIsManageableWhenWakuIsElevated) {
  WindowFacts facts = tileable();
  facts.elevated = true;
  EXPECT_EQ(classify(facts, {}, true).verdict, Verdict::Manage);
}

TEST(TilingRules, AMinimizedOrUntitledWindowIsRetried) {
  WindowFacts facts = tileable();
  facts.iconic = true;
  EXPECT_EQ(classify(facts, {}, false).verdict, Verdict::Retry);
  facts = tileable();
  facts.title.clear();
  const Eligibility untitled = classify(facts, {}, false);
  EXPECT_EQ(untitled.verdict, Verdict::Retry);
  EXPECT_EQ(untitled.reason, QStringLiteral("untitled"));
}

TEST(TilingRules, ParsesProcessClassTitleAndAction) {
  const ParsedRules parsed =
      parseRules({rule(QStringLiteral("x.exe"), QStringLiteral("ignore")),
                  QVariantMap{{QStringLiteral("class"), QStringLiteral("Dlg")},
                              {QStringLiteral("action"), QStringLiteral("float")}},
                  QVariantMap{{QStringLiteral("title"), QStringLiteral("^Settings")}}});
  EXPECT_TRUE(parsed.errors.isEmpty());
  ASSERT_EQ(parsed.rules.size(), 3u);
  EXPECT_EQ(parsed.rules[1].action, RuleAction::Float);
  EXPECT_EQ(parsed.rules[2].action, RuleAction::Ignore);  // the default
}

TEST(TilingRules, BadEntriesAreSkippedAndReported) {
  const ParsedRules parsed =
      parseRules({QVariantMap{}, QVariantMap{{QStringLiteral("title"), QStringLiteral("(")}},
                  rule(QStringLiteral("a.exe"), QStringLiteral("explode")),
                  rule(QStringLiteral("ok.exe"), QStringLiteral("float"))});
  EXPECT_EQ(parsed.errors.size(), 3);
  ASSERT_EQ(parsed.rules.size(), 1u);
  EXPECT_EQ(parsed.rules[0].process, QStringLiteral("ok.exe"));
}

TEST(TilingRules, AnIgnoreRuleMatchesTheProcessWithoutCase) {
  const auto rules =
      parseRules({rule(QStringLiteral("NOTEPAD.EXE"), QStringLiteral("ignore"))}).rules;
  EXPECT_EQ(classify(tileable(), rules, false).verdict, Verdict::Ignore);
}

TEST(TilingRules, AFloatRuleGivesFloatAndForceTileOverridesIt) {
  const auto rules =
      parseRules({rule(QStringLiteral("notepad.exe"), QStringLiteral("float"))}).rules;
  EXPECT_EQ(classify(tileable(), rules, false).verdict, Verdict::Float);
  EXPECT_EQ(classify(tileable(), rules, false, true).verdict, Verdict::Manage);
}

TEST(TilingRules, AllSetFieldsMustMatch) {
  const auto rules =
      parseRules({QVariantMap{{QStringLiteral("process"), QStringLiteral("notepad.exe")},
                              {QStringLiteral("title"), QStringLiteral("^Settings")}}})
          .rules;
  EXPECT_EQ(classify(tileable(), rules, false).verdict, Verdict::Manage);
  WindowFacts facts = tileable();
  facts.title = QStringLiteral("Settings");
  EXPECT_EQ(classify(facts, rules, false).verdict, Verdict::Ignore);
}

TEST(TilingRules, TheFirstMatchingRuleWins) {
  const auto rules = parseRules({rule(QStringLiteral("notepad.exe"), QStringLiteral("float")),
                                 rule(QStringLiteral("notepad.exe"), QStringLiteral("ignore"))})
                         .rules;
  EXPECT_EQ(matchRules(rules, tileable()), RuleAction::Float);
}

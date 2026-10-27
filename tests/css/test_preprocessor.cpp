#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/css/preprocessor.h"
#include "core/css/rules.h"

using nlohmann::json;
using namespace yasb::css;

namespace {

const json& vectors() {
  static const json v = [] {
    std::ifstream in(std::string(YASB_VECTORS_DIR) + "/css.json", std::ios::binary);
    return json::parse(in);
  }();
  return v;
}

std::vector<std::string> caseNames() {
  std::vector<std::string> names;
  for (const auto& [name, value] : vectors().items()) names.push_back(name);
  return names;
}

QString q(const std::string& s) { return QString::fromStdString(s); }

QString entryPath(const json& c) {
  return QDir::toNativeSeparators(
      QDir(QStringLiteral(YASB_VECTORS_DIR)).absoluteFilePath(q(c.at("input"))));
}

struct Dirs {
  QString dir;  // entry stylesheet folder
  QString configHome;
};

Dirs dirsFor(const QString& entry) {
  const QString dir = QFileInfo(entry).absolutePath();
  return {dir, QDir::cleanPath(dir + QStringLiteral("/../.."))};
}

// ${DIR} / ${CONFIG_HOME} placeholders; `native` picks the separators (log lines are Windows
// paths).
QString expand(QString text, const Dirs& d, bool native) {
  auto fix = [&](const QString& p) {
    return native ? QDir::toNativeSeparators(p) : QDir::fromNativeSeparators(p);
  };
  text.replace(QStringLiteral("${DIR}"), fix(d.dir));
  text.replace(QStringLiteral("${CONFIG_HOME}"), fix(d.configHome));
  return text;
}

json ruleToJson(const StyleRule& r) {
  json props = json::object();
  for (const auto& d : r.properties) props[d.first.toStdString()] = d.second.toStdString();
  json pseudo = json::array();
  for (const auto& p : r.pseudoSet) pseudo.push_back(p.toStdString());
  json segments = json::array();
  for (const auto& s : r.segments) segments.push_back(s.toStdString());
  json transitions = json::array();
  for (const auto& t : r.transitions) {
    transitions.push_back({{"delay_ms", t.delayMs},
                           {"duration_ms", t.durationMs},
                           {"easing", t.easing.toStdString()},
                           {"prop", t.prop.toStdString()}});
  }
  return {{"base_selector", r.baseSelector.toStdString()},
          {"has_attrs", r.hasAttrs},
          {"has_border_radius_props", r.hasBorderRadiusProps},
          {"has_cursor_prop", r.hasCursorProp},
          {"has_effect_props", r.hasEffectProps},
          {"order", r.order},
          {"properties", props},
          {"pseudo_set", pseudo},
          {"resets_transitions", r.resetsTransitions},
          {"scope_rank", r.scopeRank},
          {"segments", segments},
          {"selector", r.selector.toStdString()},
          {"specificity", {r.specificity[0], r.specificity[1], r.specificity[2]}},
          {"subcontrol", r.subcontrol},
          {"transitions", transitions}};
}

// Compares rule by rule so a failure names the selector instead of dumping 400 rules.
void expectRules(const ParsedStyleSheet& parsed, const json& expected, const Dirs& d) {
  ASSERT_EQ(parsed.rules.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    json want = expected[i];
    json have = ruleToJson(parsed.rules[static_cast<qsizetype>(i)]);
    if (want != have) {
      // Paths inside values are anonymised in the vectors.
      json wantProps = json::object();
      for (const auto& [k, v] : want["properties"].items()) {
        wantProps[k] = expand(q(v.get<std::string>()), d, false).toStdString();
      }
      want["properties"] = wantProps;
    }
    EXPECT_EQ(have, want) << "rule " << i << ": "
                          << parsed.rules[static_cast<qsizetype>(i)].selector.toStdString();
  }
}

class CssVector : public testing::TestWithParam<std::string> {};

}  // namespace

TEST_P(CssVector, ProcessedTextAndLogMatchPython) {
  const json& c = vectors().at(GetParam());
  const QString entry = entryPath(c);
  const Dirs d = dirsFor(entry);

  CssPreprocessor pre(entry);
  const QString processed = pre.process();
  EXPECT_EQ(processed, expand(q(c.at("processed")), d, false));

  // Python repr() doubles backslashes inside the OS error text; compare modulo that.
  auto unrepr = [](QString s) { return s.replace(QStringLiteral("\\\\"), QStringLiteral("\\")); };
  const auto& log = c.at("log");
  ASSERT_EQ(static_cast<size_t>(pre.log().size()), log.size());
  for (size_t i = 0; i < log.size(); ++i) {
    const auto& rec = pre.log()[static_cast<qsizetype>(i)];
    EXPECT_EQ(rec.level, q(log[i].at("level")));
    EXPECT_EQ(unrepr(rec.message), unrepr(expand(q(log[i].at("message")), d, true)));
  }
}

TEST_P(CssVector, ExtractRulesMatchesPython) {
  const json& c = vectors().at(GetParam());
  const Dirs d = dirsFor(entryPath(c));
  const QString processed = expand(q(c.at("processed")), d, false);

  const ParsedStyleSheet parsed = extractRules(processed);
  EXPECT_EQ(parsed.qss, expand(q(c.at("qss")), d, false));
  expectRules(parsed, c.at("rules"), d);
}

INSTANTIATE_TEST_SUITE_P(Vectors, CssVector, testing::ValuesIn(caseNames()),
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

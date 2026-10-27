#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "core/config/deprecation.h"
#include "core/config/loader.h"
#include "core/config/options/all.h"
#include "core/config/paths.h"

using nlohmann::json;
using namespace yasb::config;

namespace {

std::optional<std::string> fakeEnv(std::string_view name) {
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lower == "home") return "C:/home";
  return std::nullopt;
}

LoadResult load(std::string_view yaml) { return loadConfigText(yaml, fakeEnv); }

constexpr auto kCpuBar = R"(
bars:
  main:
    widgets:
      left: [cpu]
widgets:
  cpu:
    type: yasb.cpu.CpuWidget
    options:
)";

std::string cpuConfig(const std::string& options) { return kCpuBar + options; }

}  // namespace

TEST(Loader, EmptyFileYieldsDefaultBarAndNoWidgets) {
  const auto r = load("");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.config["bars"].size(), 1u);
  EXPECT_TRUE(r.config["bars"].contains("yasb-bar"));
  EXPECT_TRUE(r.config["widgets"].empty());
  EXPECT_TRUE(r.placedWidgets.empty());
}

TEST(Loader, MissingWidgetsKeyIsAccepted) { EXPECT_TRUE(load("watch_config: false\n").ok()); }

TEST(Loader, EnvExpansionIsCaseInsensitiveAndUndefinedBecomesEmpty) {
  const auto r = load("bars:\n  b:\n    class_name: \"$env:HOME|$Env:home|[$ENV:NOPE]\"\n");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.config["bars"]["b"]["class_name"], "C:/home|C:/home|[]");
}

TEST(Loader, StringIntegerIsCoerced) {
  const auto r = load(cpuConfig("      update_interval: \"5000\"\n"));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.widget("cpu")->status, WidgetStatus::Ok);
  EXPECT_EQ(r.widget("cpu")->options["update_interval"], 5000);
  EXPECT_TRUE(r.widget("cpu")->options["update_interval"].is_number_integer());
}

TEST(Loader, UnknownOptionDropsOnlyThatWidget) {
  const auto r = load(R"(
bars:
  main:
    widgets:
      left: [cpu, memory]
widgets:
  cpu: {type: yasb.cpu.CpuWidget, options: {bogus: 1}}
  memory: {type: yasb.memory.MemoryWidget}
)");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.widget("cpu")->status, WidgetStatus::InvalidOptions);
  ASSERT_EQ(r.widget("cpu")->errors.size(), 1u);
  EXPECT_EQ(r.widget("cpu")->errors[0].loc, json::array({"bogus"}));
  EXPECT_EQ(r.widget("cpu")->errors[0].type, "extra_forbidden");
  EXPECT_EQ(r.widget("memory")->status, WidgetStatus::Ok);
}

TEST(Loader, UnplacedWidgetIsNotValidated) {
  const auto r = load(R"(
widgets:
  cpu: {type: yasb.cpu.CpuWidget, options: {bogus: 1}}
)");
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.widgets.empty());
}

TEST(Loader, WidgetInDisabledBarIsNotValidated) {
  const auto r = load(R"(
bars:
  off:
    enabled: false
    widgets: {left: [cpu]}
widgets:
  cpu: {type: yasb.cpu.CpuWidget, options: {bogus: 1}}
)");
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.widgets.empty());
}

TEST(Loader, DeprecatedKeysAreRemovedWithWarnings) {
  const auto r =
      load(cpuConfig("      label_shadow: {color: black}\n      container_padding: {top: 1}\n"));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.widget("cpu")->status, WidgetStatus::Ok);
  ASSERT_EQ(r.warnings.size(), 2u);
  EXPECT_EQ(r.warnings[0], "[DEPRECATED] CpuConfig: 'label_shadow' - Use CSS text-shadow instead.");
  EXPECT_EQ(r.warnings[1],
            "[DEPRECATED] CpuConfig: 'container_padding' - Use CSS padding instead.");
}

TEST(Loader, RenamedKeyMovesUnlessNewNameIsPresent) {
  const auto r = load(R"(
bars:
  main:
    widgets: {left: [layout]}
widgets:
  layout:
    type: komorebi.active_layout.ActiveLayoutWidget
    options:
      layout_icons: {maximised: "[OLD]"}
)");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.widget("layout")->options["layout_icons"]["maximized"], "[OLD]");
  EXPECT_FALSE(r.widget("layout")->options["layout_icons"].contains("maximised"));
}

TEST(Loader, RenameSupportIsAppliedByDeprecationPass) {
  std::vector<std::string> warnings;
  EXPECT_TRUE(deprecation::tables().renamed.empty());
  nlohmann::ordered_json icons = {{"maximised", "x"}};
  deprecation::apply(
      "ActiveLayoutIconsConfig", icons, [](const std::string&) { return false; }, warnings);
  EXPECT_EQ(icons, (nlohmann::ordered_json{{"maximized", "x"}}));
  ASSERT_EQ(warnings.size(), 1u);
}

TEST(Loader, InvalidYamlIsAParseError) {
  const auto r = load("bars: [unclosed\n  - x: : :\n");
  EXPECT_EQ(r.status, LoadStatus::ParseError);
  EXPECT_FALSE(r.message.empty());
}

TEST(Loader, RootErrorsAreReportedWithLocation) {
  const auto r = load("watch_config: maybe\nbars:\n  main:\n    dimensions: {width: wide}\n");
  ASSERT_EQ(r.status, LoadStatus::ValidationError);
  ASSERT_EQ(r.errors.size(), 2u);
  EXPECT_EQ(r.errors[0].loc, json::array({"watch_config"}));
  EXPECT_EQ(r.errors[1].type, "value_error");
}

TEST(Loader, WidgetOutcomesForBrokenDefinitions) {
  const auto r = load(R"(
bars:
  main:
    widgets: {left: [ghost, nodots, notype, plain]}
widgets:
  nodots: {type: cpu}
  notype: {options: {}}
  plain: "yasb.cpu.CpuWidget"
)");
  EXPECT_EQ(r.widget("ghost")->status, WidgetStatus::Undefined);
  EXPECT_EQ(r.widget("nodots")->status, WidgetStatus::UnknownType);
  EXPECT_EQ(r.widget("notype")->status, WidgetStatus::MissingType);
  EXPECT_EQ(r.widget("plain")->status, WidgetStatus::Error);
}

TEST(Loader, YamlScalarsResolveLikePyYaml) {
  const auto j = parseYaml(
      "a: yes\nb: off\nc: 0o17\nd: 017\ne: 0x1F\nf: 1_000\ng: 1e3\nh: '5'\ni: ~\nj: 12:30\nk: "
      ".inf\nl: 1.5\n");
  EXPECT_EQ(j["a"], true);
  EXPECT_EQ(j["b"], false);
  EXPECT_EQ(j["c"], "0o17");  // YAML 1.1 has no 0o prefix
  EXPECT_EQ(j["d"], 15);
  EXPECT_EQ(j["e"], 31);
  EXPECT_EQ(j["f"], 1000);
  EXPECT_EQ(j["g"], "1e3");  // PyYAML float needs a dot
  EXPECT_EQ(j["h"], "5");
  EXPECT_TRUE(j["i"].is_null());
  EXPECT_EQ(j["j"], 750);
  EXPECT_TRUE(std::isinf(j["k"].get<double>()));
  EXPECT_EQ(j["l"], 1.5);
}

TEST(Loader, MergeKeysAreHonored) {
  const auto j = parseYaml("base: &b {x: 1, y: 2}\nuse:\n  <<: *b\n  y: 3\n");
  EXPECT_EQ(j["use"], (nlohmann::ordered_json{{"y", 3}, {"x", 1}}));
}

namespace {

// The std::regex resolver the scanner replaced, kept as the oracle for differential tests.
std::string oracleStrip(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
  return s;
}

OJson oracleNumber(const std::string& digits, int base, bool negative) {
  std::uint64_t magnitude = 0;
  const auto [end, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), magnitude, base);
  if (ec != std::errc() || end != digits.data() + digits.size() ||
      magnitude > static_cast<std::uint64_t>(INT64_MAX)) {
    const double d = std::strtod(digits.c_str(), nullptr);
    return negative ? -d : d;
  }
  const auto v = static_cast<std::int64_t>(magnitude);
  return negative ? -v : v;
}

OJson oracleResolve(const std::string& text) {
  static const std::regex kNull(R"(^(?:~|null|Null|NULL|)$)");
  static const std::regex kBool(
      R"(^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$)");
  static const std::regex kBin(R"(^([-+]?)0b([0-1_]+)$)");
  static const std::regex kOct(R"(^([-+]?)0([0-7_]+)$)");
  static const std::regex kDec(R"(^([-+]?)((?:0|[1-9][0-9_]*))$)");
  static const std::regex kHex(R"(^([-+]?)0x([0-9a-fA-F_]+)$)");
  static const std::regex kSexInt(R"(^([-+]?)([1-9][0-9_]*(?::[0-5]?[0-9])+)$)");
  static const std::regex kFloat(
      R"(^(?:[-+]?[0-9][0-9_]*\.[0-9_]*(?:[eE][-+][0-9]+)?|\.[0-9][0-9_]*(?:[eE][-+][0-9]+)?)$)");
  static const std::regex kSexFloat(R"(^([-+]?)([0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*)$)");
  static const std::regex kInf(R"(^([-+]?)\.(?:inf|Inf|INF)$)");
  static const std::regex kNan(R"(^\.(?:nan|NaN|NAN)$)");

  std::smatch m;
  if (std::regex_match(text, kNull)) return nullptr;
  if (std::regex_match(text, kBool)) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    return c == 'y' || c == 't' ||
           (c == 'o' && std::tolower(static_cast<unsigned char>(text[1])) == 'n');
  }
  if (std::regex_match(text, m, kBin)) return oracleNumber(oracleStrip(m[2]), 2, m[1] == "-");
  if (std::regex_match(text, m, kOct)) return oracleNumber(oracleStrip(m[2]), 8, m[1] == "-");
  if (std::regex_match(text, m, kDec)) return oracleNumber(oracleStrip(m[2]), 10, m[1] == "-");
  if (std::regex_match(text, m, kHex)) return oracleNumber(oracleStrip(m[2]), 16, m[1] == "-");
  if (std::regex_match(text, m, kSexInt) || std::regex_match(text, m, kSexFloat)) {
    const bool isFloat = text.find('.') != std::string::npos;
    std::string body = oracleStrip(m[2]);
    double total = 0;
    std::stringstream parts(body);
    std::string part;
    while (std::getline(parts, part, ':')) total = total * 60 + std::strtod(part.c_str(), nullptr);
    if (m[1] == "-") total = -total;
    return isFloat ? OJson(total) : OJson(static_cast<std::int64_t>(total));
  }
  if (std::regex_match(text, kFloat)) return std::strtod(oracleStrip(text).c_str(), nullptr);
  if (std::regex_match(text, m, kInf)) return m[1] == "-" ? -HUGE_VAL : HUGE_VAL;
  if (std::regex_match(text, kNan)) return std::nan("");
  return text;
}

// Type plus dump, so NaN (dumped as null) and null stay distinct.
std::string describe(const OJson& j) {
  return std::string(j.type_name()) + ":" +
         (j.is_number_float() && std::isnan(j.get<double>()) ? std::string("nan") : j.dump());
}

}  // namespace

TEST(ScalarScanner, MatchesRegexResolverOnEveryShortString) {
  const std::string alphabet = "016_:.-+exb";  // the curated cases cover E, 8, a-f
  std::vector<std::string> inputs{""};
  for (std::size_t begin = 0, len = 1; len <= 4; ++len) {
    const std::size_t end = inputs.size();
    for (std::size_t i = begin; i < end; ++i) {
      for (const char c : alphabet) inputs.push_back(inputs[i] + c);
    }
    begin = end;
  }
  for (const auto& s : inputs) {
    ASSERT_EQ(describe(resolvePlainScalar(s)), describe(oracleResolve(s))) << "input: " << s;
  }
}

TEST(ScalarScanner, MatchesRegexResolverOnEveryBranch) {
  const std::vector<std::string> cases{"~",
                                       "null",
                                       "Null",
                                       "NULL",
                                       "nULL",
                                       "yes",
                                       "Yes",
                                       "YES",
                                       "yEs",
                                       "no",
                                       "No",
                                       "NO",
                                       "true",
                                       "True",
                                       "TRUE",
                                       "false",
                                       "False",
                                       "FALSE",
                                       "on",
                                       "On",
                                       "ON",
                                       "oN",
                                       "off",
                                       "Off",
                                       "OFF",
                                       "y",
                                       "n",
                                       "0b",
                                       "0b1010",
                                       "-0b1_0",
                                       "+0b2",
                                       "0B1",
                                       "0",
                                       "-0",
                                       "+0",
                                       "00",
                                       "0_",
                                       "017",
                                       "-0_17",
                                       "08",
                                       "1_000",
                                       "-1_000",
                                       "+42",
                                       "9223372036854775807",
                                       "9223372036854775808",
                                       "-9223372036854775808",
                                       "99999999999999999999999",
                                       "0x",
                                       "0x1F",
                                       "0X1F",
                                       "-0xff_FF",
                                       "0xg",
                                       "0x_",
                                       "12:30",
                                       "1:2:3",
                                       "-1:30",
                                       "1:60",
                                       "1:59",
                                       "1:5",
                                       "1:",
                                       ":30",
                                       "01:30",
                                       "1_0:30",
                                       "1:3_0",
                                       "12:30.5",
                                       "0:30.5",
                                       "-1:30.",
                                       "1:30.5_5",
                                       "1:30.5e+3",
                                       "1.",
                                       "1.5",
                                       "-1.5",
                                       "+1.5",
                                       "1_0.5_0",
                                       "1.5e+3",
                                       "1.5E-3",
                                       "1.5e3",
                                       "1.5e+",
                                       "1e+3",
                                       ".5",
                                       ".5e+3",
                                       "-.5",
                                       "._5",
                                       ".",
                                       "-.",
                                       ".e+1",
                                       "1.5e+3x",
                                       ".inf",
                                       ".Inf",
                                       ".INF",
                                       "-.inf",
                                       "+.Inf",
                                       ".iNF",
                                       "inf",
                                       ".nan",
                                       ".NaN",
                                       ".NAN",
                                       "-.nan",
                                       ".Nan",
                                       "nan",
                                       "-",
                                       "+",
                                       "-+1",
                                       "1 ",
                                       " 1",
                                       "1\n",
                                       "\xC3\xA9",
                                       "1\xC3\xA9"};
  for (const auto& s : cases) {
    EXPECT_EQ(describe(resolvePlainScalar(s)), describe(oracleResolve(s))) << "input: " << s;
  }
}

TEST(ScalarScanner, LongScalarsDoNotRecurse) {
  // 100k-character inputs used to risk overflowing std::regex's recursive matcher on MSVC.
  const std::string digits(100000, '1');
  EXPECT_TRUE(resolvePlainScalar(digits).is_number_float());
  EXPECT_TRUE(resolvePlainScalar("0" + std::string(100000, '7')).is_number_float());
  EXPECT_EQ(resolvePlainScalar(std::string(100000, '_').insert(0, "1")), 1);
  EXPECT_EQ(resolvePlainScalar("0b" + std::string(99999, '0') + "1"), 1);
  EXPECT_EQ(resolvePlainScalar(std::string(100000, 'a')), std::string(100000, 'a'));
  std::string sexagesimal = "0";
  for (int i = 0; i < 33333; ++i) sexagesimal += ":00";
  EXPECT_EQ(resolvePlainScalar(sexagesimal + "."), 0.0);
  EXPECT_EQ(resolvePlainScalar(sexagesimal), sexagesimal) << "int form needs a 1-9 lead";
  EXPECT_EQ(resolvePlainScalar("1." + std::string(100000, '0')), 1.0);
  EXPECT_EQ(resolvePlainScalar("1." + std::string(100000, '0') + "x"),
            "1." + std::string(100000, '0') + "x");
}

TEST(Loader, TypedOptionsFromNormalizedConfig) {
  const auto r = load(cpuConfig("      update_interval: 2000\n      cpu_thresholds: {high: 80}\n"));
  const auto cpu = r.widgetOptions<options::CpuConfig>("cpu");
  ASSERT_TRUE(cpu.has_value());
  EXPECT_EQ(cpu->update_interval, 2000);
  EXPECT_EQ(cpu->cpu_thresholds.high, 80);
  EXPECT_EQ(cpu->cpu_thresholds.low, 25);
  EXPECT_EQ(cpu->callbacks.on_left, "toggle_label");
  EXPECT_EQ(cpu->histogram_icons.size(), 9u);
}

TEST(Loader, TypedOptionsTypeMismatchIsNulloptNotAThrow) {
  LoadResult r;
  WidgetResult w;
  w.status = WidgetStatus::Ok;
  w.type = "yasb.cpu.CpuWidget";
  w.options = {{"update_interval", "fast"}};
  r.widgets["cpu"] = w;
  EXPECT_FALSE(r.widgetOptions<options::CpuConfig>("cpu").has_value());
}

// Model validators run on whatever the schema produced; a schema without the field they inspect
// must not reach the const json::operator[] on a missing key.
TEST(Loader, ModelValidatorsTolerateMissingFields) {
  const SchemaValidator validator(OJson::parse(R"({
    "$defs": {
      "BarDimensions": {"type": "object", "properties": {"height": {"type": "integer"}}},
      "QuickActionsSectionConfig": {"type": "object", "properties": {}}
    },
    "type": "object",
    "properties": {
      "dims": {"$ref": "#/$defs/BarDimensions"},
      "actions": {"$ref": "#/$defs/QuickActionsSectionConfig"}
    }
  })"));
  const Validated v =
      validator.validateRoot(OJson::parse(R"({"dims": {"height": 5}, "actions": {}})"));
  EXPECT_TRUE(v.ok());
  EXPECT_FALSE(validator.knowsWidgetType("yasb.cpu.CpuWidget"));
}

TEST(Loader, ValidatorRejectsOutOfRangeAndWrongTypes) {
  const auto r = load(cpuConfig("      update_interval: 10\n      label: 5\n"));
  const auto* w = r.widget("cpu");
  ASSERT_EQ(w->status, WidgetStatus::InvalidOptions);
  ASSERT_EQ(w->errors.size(), 2u);
  EXPECT_EQ(w->errors[0].type, "string_type");
  EXPECT_EQ(w->errors[1].type, "greater_than_equal");
}

TEST(Paths, ConfigHomeOverrideWins) {
  _putenv_s("YASB_CONFIG_HOME", "D:\\cfg-home");
  EXPECT_EQ(configPath(), std::filesystem::path("D:\\cfg-home") / "config.yaml");
  EXPECT_EQ(stylesheetPath(), std::filesystem::path("D:\\cfg-home") / "styles.css");
  _putenv_s("YASB_CONFIG_HOME", "");
  EXPECT_EQ(configDir().filename(), "yasb");
  EXPECT_EQ(dataDir().filename(), "YASB");
}

TEST(Loader, MissingFileIsNotFound) {
  EXPECT_EQ(loadConfigFile("D:\\definitely\\not\\here.yaml").status, LoadStatus::NotFound);
}

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/config/deprecation.h"
#include "core/config/loader.h"
#include "core/config/options/all.h"
#include "core/config/paths.h"

using nlohmann::json;
using namespace yasb::config;

namespace {

std::optional<std::string> fakeEnv(std::string_view name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
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

TEST(Loader, MissingWidgetsKeyIsAccepted) {
    EXPECT_TRUE(load("watch_config: false\n").ok());
}

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
    const auto r = load(cpuConfig("      label_shadow: {color: black}\n      container_padding: {top: 1}\n"));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.widget("cpu")->status, WidgetStatus::Ok);
    ASSERT_EQ(r.warnings.size(), 2u);
    EXPECT_EQ(r.warnings[0], "[DEPRECATED] CpuConfig: 'label_shadow' - Use CSS text-shadow instead.");
    EXPECT_EQ(r.warnings[1], "[DEPRECATED] CpuConfig: 'container_padding' - Use CSS padding instead.");
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
    deprecation::apply("ActiveLayoutIconsConfig", icons, [](const std::string&) { return false; }, warnings);
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
    const auto j = parseYaml("a: yes\nb: off\nc: 0o17\nd: 017\ne: 0x1F\nf: 1_000\ng: 1e3\nh: '5'\ni: ~\nj: 12:30\nk: .inf\nl: 1.5\n");
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

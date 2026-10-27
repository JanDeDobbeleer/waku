#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include "core/config/deprecation.h"
#include "core/config/loader.h"

using nlohmann::json;
using namespace yasb::config;

namespace {

const std::filesystem::path kVectors = std::filesystem::path(YASB_VECTORS_DIR);

json readJson(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return json::parse(in);
}

const json& vectors() {
    static const json all = readJson(kVectors / "config.json");
    return all;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// The exporter defines YASB_PARITY_VAR and leaves YASB_PARITY_UNDEFINED unset.
std::optional<std::string> parityEnv(std::string_view name) {
    std::string upper(name);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
    if (upper == "YASB_PARITY_VAR") return "from-env";
    return std::nullopt;
}

json issuesToJson(const std::vector<Issue>& issues) {
    json out = json::array();
    for (const auto& i : issues) out.push_back({{"loc", i.loc}, {"msg", i.msg}, {"type", i.type}});
    return out;
}

json pythonIssues(const json& errors) {
    json out = json::array();
    for (const auto& e : errors) out.push_back({{"loc", e["loc"]}, {"msg", e["msg"]}, {"type", e["type"]}});
    return out;
}

std::vector<std::string> pythonWarnings(const json& records) {
    std::vector<std::string> out;
    for (const auto& r : records) out.push_back(r["message"].get<std::string>());
    return out;
}

const char* statusName(WidgetStatus s) {
    switch (s) {
        case WidgetStatus::Ok: return "ok";
        case WidgetStatus::InvalidOptions: return "invalid_options";
        case WidgetStatus::UnknownType: return "unknown_type";
        case WidgetStatus::MissingType: return "missing_type";
        case WidgetStatus::Undefined: return "undefined";
        case WidgetStatus::Error: return "error";
    }
    return "";
}

std::vector<std::string> caseNames() {
    std::vector<std::string> names;
    for (auto it = vectors().begin(); it != vectors().end(); ++it) names.push_back(it.key());
    return names;
}

class ConfigParity : public testing::TestWithParam<std::string> {};

}  // namespace

TEST(ConfigParityVectors, CoversEveryManifestCase) {
    const json manifest = readJson(kVectors / "manifest.json");
    EXPECT_EQ(vectors().size(), manifest["counts"]["config_cases"].get<std::size_t>());
}

TEST(ConfigParityVectors, DeprecationTablesMatchPython) {
    const json py = readJson(kVectors / "deprecations.json");
    const auto& t = deprecation::tables();
    EXPECT_EQ(json(t.deprecated), py["deprecated_fields"]);
    EXPECT_EQ(json(t.renamed), py["renamed_fields"]);
    EXPECT_EQ(json(t.scopedDeprecated), py["scoped_deprecated_fields"]);
    EXPECT_EQ(json(t.scopedRenamed), py["scoped_renamed_fields"]);
}

// Numbers are compared as nlohmann::json values: integral doubles equal the same integer, so Python's
// 5000 vs 5000.0 spelling does not matter, only the value does.
TEST_P(ConfigParity, MatchesPythonNormalizedOutput) {
    const std::string name = GetParam();
    const json& expected = vectors()[name];
    const std::string yaml = readText(kVectors / expected["input"].get<std::string>());

    const LoadResult result = loadConfigText(yaml, parityEnv);
    const std::string status = expected["status"];

    if (status == "parse_error") {
        EXPECT_EQ(result.status, LoadStatus::ParseError);
        return;
    }
    EXPECT_EQ(result.rootIsMapping, expected["root_is_mapping"].get<bool>());
    EXPECT_EQ(result.warnings, pythonWarnings(expected["warnings"]));

    if (status == "validation_error") {
        ASSERT_EQ(result.status, LoadStatus::ValidationError);
        EXPECT_EQ(issuesToJson(result.errors), pythonIssues(expected["errors"]));
        return;
    }

    ASSERT_EQ(result.status, LoadStatus::Ok) << formatIssues(result.errors);
    EXPECT_EQ(result.config, expected["normalized"]);

    std::vector<std::string> placed = result.placedWidgets;
    std::sort(placed.begin(), placed.end());
    EXPECT_EQ(json(placed), expected["placed_widgets"]);

    json barsByWidget = json::object();
    for (const auto& [widget, outcome] : result.widgets) barsByWidget[widget] = outcome.bars;
    EXPECT_EQ(barsByWidget, expected["widget_bars"]);

    ASSERT_EQ(result.widgets.size(), expected["widgets"].size());
    for (auto it = expected["widgets"].begin(); it != expected["widgets"].end(); ++it) {
        SCOPED_TRACE("widget " + it.key());
        const WidgetResult* actual = result.widget(it.key());
        ASSERT_NE(actual, nullptr);
        const json& want = it.value();
        EXPECT_STREQ(statusName(actual->status), want["status"].get<std::string>().c_str());
        if (want.contains("type")) EXPECT_EQ(actual->type, want["type"].get<std::string>());
        if (want["status"] == "ok") EXPECT_EQ(actual->options, want["normalized"]);
        if (want["status"] == "invalid_options") {
            EXPECT_EQ(issuesToJson(actual->errors), pythonIssues(want["errors"]));
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Vectors, ConfigParity, testing::ValuesIn(caseNames()),
                         [](const testing::TestParamInfo<std::string>& info) { return info.param; });

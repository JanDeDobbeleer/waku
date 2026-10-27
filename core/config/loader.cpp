#include "core/config/loader.h"

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

#include "core/config/paths.h"

Q_LOGGING_CATEGORY(lcConfig, "yasb.config")

namespace yasb::config {

namespace {

using nlohmann::json;

bool isWordByte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

bool iequalsPrefix(std::string_view s, std::size_t pos, std::string_view lower) {
    if (pos + lower.size() > s.size()) return false;
    for (std::size_t i = 0; i < lower.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[pos + i])) != lower[i]) return false;
    }
    return true;
}

std::string expandString(const std::string& s, const EnvLookup& lookup) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '$' && iequalsPrefix(s, i + 1, "env:")) {
            std::size_t end = i + 5;
            while (end < s.size() && isWordByte(static_cast<unsigned char>(s[end]))) ++end;
            if (end > i + 5) {
                const std::string name = s.substr(i + 5, end - (i + 5));
                if (auto value = lookup(name)) out += *value;
                i = end;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

std::string stripUnderscores(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
    return s;
}

OJson numberFromDigits(const std::string& digits, int base, bool negative) {
    std::uint64_t magnitude = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), magnitude, base);
    if (ec != std::errc() || end != digits.data() + digits.size() ||
        magnitude > static_cast<std::uint64_t>(INT64_MAX)) {
        const double d = std::strtod(digits.c_str(), nullptr);  // beyond int64: PyYAML keeps a bigint
        return negative ? -d : d;
    }
    const auto v = static_cast<std::int64_t>(magnitude);
    return negative ? -v : v;
}

// PyYAML (YAML 1.1) implicit scalar resolution for plain scalars.
OJson resolvePlainScalar(const std::string& text) {
    static const std::regex kNull(R"(^(?:~|null|Null|NULL|)$)");
    static const std::regex kBool(R"(^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$)");
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
        return c == 'y' || c == 't' || (c == 'o' && std::tolower(static_cast<unsigned char>(text[1])) == 'n');
    }
    if (std::regex_match(text, m, kBin)) return numberFromDigits(stripUnderscores(m[2]), 2, m[1] == "-");
    if (std::regex_match(text, m, kOct)) return numberFromDigits(stripUnderscores(m[2]), 8, m[1] == "-");
    if (std::regex_match(text, m, kDec)) return numberFromDigits(stripUnderscores(m[2]), 10, m[1] == "-");
    if (std::regex_match(text, m, kHex)) return numberFromDigits(stripUnderscores(m[2]), 16, m[1] == "-");
    if (std::regex_match(text, m, kSexInt) || std::regex_match(text, m, kSexFloat)) {
        const bool isFloat = text.find('.') != std::string::npos;
        std::string body = stripUnderscores(m[2]);
        double total = 0;
        std::stringstream parts(body);
        std::string part;
        while (std::getline(parts, part, ':')) total = total * 60 + std::strtod(part.c_str(), nullptr);
        if (m[1] == "-") total = -total;
        return isFloat ? OJson(total) : OJson(static_cast<std::int64_t>(total));
    }
    if (std::regex_match(text, kFloat)) return std::strtod(stripUnderscores(text).c_str(), nullptr);
    if (std::regex_match(text, m, kInf)) {
        return m[1] == "-" ? -HUGE_VAL : HUGE_VAL;
    }
    if (std::regex_match(text, kNan)) return std::nan("");
    return text;
}

OJson convert(const YAML::Node& node);

void mergeInto(OJson& target, const YAML::Node& source) {
    if (source.IsMap()) {
        OJson merged = convert(source);
        for (auto it = merged.begin(); it != merged.end(); ++it) {
            if (!target.contains(it.key())) target[it.key()] = it.value();
        }
    } else if (source.IsSequence()) {
        for (const auto& item : source) mergeInto(target, item);
    }
}

std::string keyText(const YAML::Node& key) {
    const OJson k = convert(key);
    if (k.is_string()) return k.get<std::string>();
    return k.is_null() ? "null" : k.dump();
}

OJson convert(const YAML::Node& node) {
    switch (node.Type()) {
        case YAML::NodeType::Null:
        case YAML::NodeType::Undefined:
            return nullptr;
        case YAML::NodeType::Scalar: {
            const std::string& tag = node.Tag();
            if (tag == "?") return resolvePlainScalar(node.Scalar());
            if (tag == "tag:yaml.org,2002:str" || tag == "!") return node.Scalar();
            return resolvePlainScalar(node.Scalar());
        }
        case YAML::NodeType::Sequence: {
            OJson array = OJson::array();
            for (const auto& item : node) array.push_back(convert(item));
            return array;
        }
        case YAML::NodeType::Map: {
            OJson object = OJson::object();
            std::vector<YAML::Node> merges;
            for (const auto& kv : node) {
                if (kv.first.IsScalar() && kv.first.Tag() == "?" && kv.first.Scalar() == "<<") {
                    merges.push_back(kv.second);
                    continue;
                }
                object[keyText(kv.first)] = convert(kv.second);
            }
            for (const auto& source : merges) mergeInto(object, source);
            return object;
        }
    }
    return nullptr;
}

std::string locText(const json& loc) {
    std::string out;
    for (const auto& part : loc) {
        if (part.is_number()) {
            out += "[" + part.dump() + "]";
        } else {
            if (!out.empty()) out += '.';
            out += part.get<std::string>();
        }
    }
    return out;
}

EnvLookup defaultEnv() {
    return [](std::string_view name) { return environmentVariable(name); };
}

std::vector<std::string> placedBarWidgets(const OJson& bars, std::vector<std::string>& order,
                                          std::map<std::string, std::vector<std::string>>& barsByWidget) {
    for (auto bar = bars.begin(); bar != bars.end(); ++bar) {
        if (!bar.value().value("enabled", true)) continue;
        const auto& placement = bar.value()["widgets"];
        for (const char* column : {"left", "center", "right"}) {
            for (const auto& name : placement[column]) {
                const auto widgetName = name.get<std::string>();
                auto& list = barsByWidget[widgetName];
                if (list.empty()) order.push_back(widgetName);
                if (std::find(list.begin(), list.end(), bar.key()) == list.end()) list.push_back(bar.key());
            }
        }
    }
    return order;
}

WidgetResult validateWidget(const SchemaValidator& validator, const OJson& widgets, const std::string& name,
                            std::vector<std::string>& warnings) {
    WidgetResult result;
    const auto found = widgets.find(name);
    const bool empty = found == widgets.end() || found->is_null() || (found->is_string() && found->empty()) ||
                       (found->is_object() && found->empty());
    if (empty) return result;  // Undefined

    if (!found->is_object()) {
        result.status = WidgetStatus::Error;
        return result;
    }
    const auto typeIt = found->find("type");
    if (typeIt == found->end()) {
        result.status = WidgetStatus::MissingType;
        return result;
    }
    result.status = WidgetStatus::UnknownType;
    if (!typeIt->is_string()) return result;
    result.type = typeIt->get<std::string>();
    if (result.type.find('.') == std::string::npos || !validator.knowsWidgetType(result.type)) return result;

    const OJson options = found->contains("options") ? (*found)["options"] : OJson::object();
    Validated validated = validator.validateWidgetOptions(result.type, options);
    warnings.insert(warnings.end(), validated.warnings.begin(), validated.warnings.end());
    if (!validated.ok()) {
        result.status = WidgetStatus::InvalidOptions;
        result.errors = std::move(validated.issues);
        return result;
    }
    result.status = WidgetStatus::Ok;
    result.options = json(validated.value);
    return result;
}

}  // namespace

const WidgetResult* LoadResult::widget(const std::string& name) const {
    const auto it = widgets.find(name);
    return it == widgets.end() ? nullptr : &it->second;
}

OJson expandEnvironment(const OJson& value, const EnvLookup& lookup) {
    if (value.is_string()) return expandString(value.get<std::string>(), lookup);
    if (value.is_array()) {
        OJson out = OJson::array();
        for (const auto& item : value) out.push_back(expandEnvironment(item, lookup));
        return out;
    }
    if (value.is_object()) {
        OJson out = OJson::object();
        for (auto it = value.begin(); it != value.end(); ++it) out[it.key()] = expandEnvironment(it.value(), lookup);
        return out;
    }
    return value;
}

OJson parseYaml(std::string_view text) {
    const YAML::Node root = YAML::Load(std::string(text));
    return convert(root);
}

std::string formatIssues(const std::vector<Issue>& issues) {
    std::string out;
    for (const auto& issue : issues) out += locText(issue.loc) + ": " + issue.msg + "\n";
    return out;
}

LoadResult loadConfigText(std::string_view yaml, const EnvLookup& env) {
    LoadResult result;
    OJson raw;
    try {
        raw = parseYaml(yaml);
    } catch (const YAML::Exception& error) {
        result.status = LoadStatus::ParseError;
        result.message = error.what();
        qCWarning(lcConfig).noquote() << "The config contains parser error(s):" << QString::fromStdString(result.message);
        return result;
    }
    if (raw.is_null()) raw = OJson::object();
    raw = expandEnvironment(raw, env ? env : defaultEnv());
    result.rootIsMapping = raw.is_object();
    if (!result.rootIsMapping) raw = OJson::object();

    const SchemaValidator& validator = SchemaValidator::embedded();
    Validated root = validator.validateRoot(raw);
    result.warnings = std::move(root.warnings);
    if (!root.ok()) {
        result.status = LoadStatus::ValidationError;
        result.errors = std::move(root.issues);
        qCWarning(lcConfig).noquote() << "The config contains validation errors:\n"
                                      << QString::fromStdString(formatIssues(result.errors));
        for (const auto& w : result.warnings) qCWarning(lcConfig).noquote() << QString::fromStdString(w);
        return result;
    }
    result.config = json(root.value);

    std::map<std::string, std::vector<std::string>> barsByWidget;
    placedBarWidgets(root.value["bars"], result.placedWidgets, barsByWidget);
    for (const auto& name : result.placedWidgets) {
        WidgetResult widget = validateWidget(validator, root.value["widgets"], name, result.warnings);
        widget.bars = barsByWidget[name];
        switch (widget.status) {
            case WidgetStatus::InvalidOptions:
                qCWarning(lcConfig).noquote() << "Widget" << QString::fromStdString(name)
                                              << "has invalid options and is skipped:\n"
                                              << QString::fromStdString(formatIssues(widget.errors));
                break;
            case WidgetStatus::UnknownType:
                qCWarning(lcConfig).noquote() << "Widget" << QString::fromStdString(name) << "has unknown type"
                                              << QString::fromStdString(widget.type);
                break;
            case WidgetStatus::MissingType:
            case WidgetStatus::Error:
                qCWarning(lcConfig).noquote() << "Widget" << QString::fromStdString(name) << "has no valid type";
                break;
            case WidgetStatus::Undefined:
                qCWarning(lcConfig).noquote() << "No widget config could be found for widget"
                                              << QString::fromStdString(name);
                break;
            case WidgetStatus::Ok:
                break;
        }
        result.widgets.emplace(name, std::move(widget));
    }
    for (const auto& w : result.warnings) qCWarning(lcConfig).noquote() << QString::fromStdString(w);
    return result;
}

LoadResult loadConfigFile(const std::filesystem::path& file, const EnvLookup& env) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        LoadResult result;
        result.status = LoadStatus::NotFound;
        result.message = "The file could not be found";
        qCWarning(lcConfig).noquote() << "The file" << QString::fromStdWString(file.wstring())
                                      << "could not be found. Does it exist?";
        return result;
    }
    std::ifstream stream(file, std::ios::binary);
    std::stringstream buffer;
    if (stream) buffer << stream.rdbuf();
    if (!stream) {
        LoadResult result;
        result.status = LoadStatus::ReadError;
        result.message = "The file could not be read";
        qCWarning(lcConfig).noquote() << "The file" << QString::fromStdWString(file.wstring()) << "could not be read.";
        return result;
    }
    return loadConfigText(buffer.str(), env);
}

}  // namespace yasb::config

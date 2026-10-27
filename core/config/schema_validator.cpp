#include "core/config/schema_validator.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

#include "core/config/deprecation.h"

namespace yasb::config {

// Defined in the generated schema_data.cpp (schema/schema.json compiled into the binary).
extern const unsigned char kEmbeddedSchema[];
extern const std::size_t kEmbeddedSchemaSize;

namespace {

using nlohmann::json;

struct Ctx {
    std::vector<Issue>* issues;
    std::vector<std::string>* warnings;
    std::vector<json> loc;
};

void report(Ctx& c, std::string type, std::string msg) {
    c.issues->push_back(Issue{json(c.loc), std::move(type), std::move(msg)});
}

std::string quoteLiteral(const OJson& v) {
    return v.is_string() ? "'" + v.get<std::string>() + "'" : v.dump();
}

std::string literalMessage(const std::vector<OJson>& options) {
    std::string out;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i > 0) out += (i + 1 == options.size()) ? " or " : ", ";
        out += quoteLiteral(options[i]);
    }
    return "Input should be " + out;
}

std::string trim(std::string_view s) {
    const char* ws = " \t\n\r\f\v";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return std::string(s.substr(b, e - b + 1));
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size();
}

class Walker {
public:
    explicit Walker(const OJson& defs) : m_defs(defs) {}

    bool check(const OJson& schema, const OJson& v, bool strict, Ctx& ctx, OJson& out,
               std::string_view model = {}) const {
        if (auto ref = schema.find("$ref"); ref != schema.end()) {
            const std::string name = ref->get<std::string>().substr(std::string("#/$defs/").size());
            auto def = m_defs.find(name);
            if (def == m_defs.end()) {
                report(ctx, "schema_error", "Unresolved reference " + name);
                return false;
            }
            return check(*def, v, strict, ctx, out, name);
        }
        if (schema.contains("anyOf")) return checkUnion(schema["anyOf"], v, strict, ctx, out);
        if (schema.contains("const")) {
            if (v == schema["const"]) {
                out = v;
                return true;
            }
            report(ctx, "literal_error", literalMessage({schema["const"]}));
            return false;
        }
        if (schema.contains("enum")) {
            const auto& options = schema["enum"];
            if (std::find(options.begin(), options.end(), v) != options.end()) {
                out = v;
                return true;
            }
            report(ctx, "literal_error", literalMessage(options.get<std::vector<OJson>>()));
            return false;
        }

        const auto typeIt = schema.find("type");
        if (typeIt == schema.end()) {
            out = v;
            return true;
        }
        const std::string type = typeIt->get<std::string>();
        if (type == "object") return checkObject(schema, v, strict, ctx, out, model);
        if (type == "array") return checkArray(schema, v, strict, ctx, out);
        if (type == "string") {
            if (v.is_string()) {
                out = v;
                return true;
            }
            report(ctx, "string_type", "Input should be a valid string");
            return false;
        }
        if (type == "integer") return checkInt(schema, v, strict, ctx, out);
        if (type == "number") return checkNumber(schema, v, strict, ctx, out);
        if (type == "boolean") return checkBool(v, strict, ctx, out);
        if (type == "null") {
            if (v.is_null()) {
                out = v;
                return true;
            }
            report(ctx, "none_required", "Input should be None");
            return false;
        }
        out = v;
        return true;
    }

private:
    const OJson& m_defs;

    // pydantic's smart union: an exact (strict) match wins, then the first member that coerces.
    bool checkUnion(const OJson& members, const OJson& v, bool strict, Ctx& ctx, OJson& out) const {
        for (const bool pass : {true, false}) {
            if (!pass && strict) break;
            for (const auto& member : members) {
                std::vector<Issue> issues;
                std::vector<std::string> warnings;
                Ctx attempt{&issues, &warnings, ctx.loc};
                OJson value;
                if (check(member, v, pass, attempt, value)) {
                    ctx.warnings->insert(ctx.warnings->end(), warnings.begin(), warnings.end());
                    out = std::move(value);
                    return true;
                }
            }
        }
        for (const auto& member : members) {
            std::vector<Issue> issues;
            std::vector<std::string> warnings;
            Ctx attempt{&issues, &warnings, ctx.loc};
            OJson value;
            check(member, v, strict, attempt, value);
            ctx.issues->insert(ctx.issues->end(), issues.begin(), issues.end());
        }
        return false;
    }

    bool checkObject(const OJson& schema, const OJson& v, bool strict, Ctx& ctx, OJson& out,
                     std::string_view model) const {
        const bool isModel = schema.contains("properties");
        if (!v.is_object()) {
            report(ctx, isModel ? "model_type" : "dict_type",
                   isModel ? "Input should be a valid dictionary or object to construct model instance"
                           : "Input should be a valid dictionary");
            return false;
        }
        if (isModel) return checkModel(schema, v, strict, ctx, out, model);

        const auto extra = schema.find("additionalProperties");
        out = OJson::object();
        bool ok = true;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (extra != schema.end() && extra->is_object()) {
                ctx.loc.push_back(it.key());
                OJson child;
                if (check(*extra, it.value(), strict, ctx, child)) {
                    out[it.key()] = std::move(child);
                } else {
                    ok = false;
                }
                ctx.loc.pop_back();
            } else {
                out[it.key()] = it.value();
            }
        }
        return ok;
    }

    bool checkModel(const OJson& schema, const OJson& v, bool strict, Ctx& ctx, OJson& out,
                    std::string_view model) const {
        OJson data = v;
        const auto& props = schema["properties"];
        deprecation::apply(
            model, data, [&](const std::string& key) { return props.contains(key); }, *ctx.warnings);

        std::vector<std::string> required;
        if (schema.contains("required")) required = schema["required"].get<std::vector<std::string>>();

        out = OJson::object();
        bool ok = true;
        for (auto it = props.begin(); it != props.end(); ++it) {
            const std::string& name = it.key();
            ctx.loc.push_back(name);
            OJson child;
            if (auto given = data.find(name); given != data.end()) {
                if (check(it.value(), *given, strict, ctx, child)) {
                    out[name] = std::move(child);
                } else {
                    ok = false;
                }
            } else if (it.value().contains("default")) {
                // pydantic runs with validate_default=True
                if (check(it.value(), it.value()["default"], false, ctx, child)) {
                    out[name] = std::move(child);
                } else {
                    ok = false;
                }
            } else if (std::find(required.begin(), required.end(), name) != required.end()) {
                report(ctx, "missing", "Field required");
                ok = false;
            }
            ctx.loc.pop_back();
        }

        const auto extra = schema.find("additionalProperties");
        const bool forbidExtra = extra != schema.end() && extra->is_boolean() && !extra->get<bool>();
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (props.contains(it.key())) continue;
            if (forbidExtra) {
                ctx.loc.push_back(it.key());
                report(ctx, "extra_forbidden", "Extra inputs are not permitted");
                ctx.loc.pop_back();
                ok = false;
            } else {
                out[it.key()] = it.value();
            }
        }

        if (ok) ok = runModelValidators(model, out, ctx);
        return ok;
    }

    // Validators declared in the Python models that schema.json cannot express.
    static bool runModelValidators(std::string_view model, const OJson& value, Ctx& ctx) {
        if (model == "BarDimensions") {
            const auto& width = value["width"];
            constexpr auto kMessage =
                "Value error, Width must be an integer, 'auto', or a percentage string (e.g. '100%')";
            std::string error;
            if (width.is_number_integer()) {
                if (width.get<std::int64_t>() < 0) error = "Value error, Width must be non-negative";
            } else if (width.is_string()) {
                const auto s = width.get<std::string>();
                const bool percent = s.size() > 1 && s.back() == '%' &&
                                     std::all_of(s.begin(), s.end() - 1, [](unsigned char c) {
                                         return c >= '0' && c <= '9';
                                     });
                if (s != "auto" && !percent) error = kMessage;
            }
            if (!error.empty()) {
                ctx.loc.push_back("width");
                report(ctx, "value_error", error);
                ctx.loc.pop_back();
                return false;
            }
        } else if (model == "QuickActionsSectionConfig") {
            std::vector<std::string> seen;
            for (const auto& action : value["actions"]) {
                const auto id = action.value("id", std::string());
                if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
                    ctx.loc.push_back("actions");
                    report(ctx, "value_error", "Value error, Duplicate action id: " + id);
                    ctx.loc.pop_back();
                    return false;
                }
                seen.push_back(id);
            }
        }
        return true;
    }

    bool checkArray(const OJson& schema, const OJson& v, bool strict, Ctx& ctx, OJson& out) const {
        if (!v.is_array()) {
            report(ctx, "list_type", "Input should be a valid list");
            return false;
        }
        out = OJson::array();
        bool ok = true;
        const auto items = schema.find("items");
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (items == schema.end()) {
                out.push_back(v[i]);
                continue;
            }
            ctx.loc.push_back(i);
            OJson child;
            if (check(*items, v[i], strict, ctx, child)) {
                out.push_back(std::move(child));
            } else {
                ok = false;
            }
            ctx.loc.pop_back();
        }
        if (!ok) return false;
        if (schema.contains("minItems") && v.size() < schema["minItems"].get<std::size_t>()) {
            report(ctx, "too_short",
                   "List should have at least " + schema["minItems"].dump() + " items after validation, not " +
                       std::to_string(v.size()));
            return false;
        }
        if (schema.contains("maxItems") && v.size() > schema["maxItems"].get<std::size_t>()) {
            report(ctx, "too_long",
                   "List should have at most " + schema["maxItems"].dump() + " items after validation, not " +
                       std::to_string(v.size()));
            return false;
        }
        return true;
    }

    static bool checkRange(const OJson& schema, double value, Ctx& ctx) {
        if (schema.contains("minimum") && value < schema["minimum"].get<double>()) {
            report(ctx, "greater_than_equal",
                   "Input should be greater than or equal to " + schema["minimum"].dump());
            return false;
        }
        if (schema.contains("exclusiveMinimum") && value <= schema["exclusiveMinimum"].get<double>()) {
            report(ctx, "greater_than", "Input should be greater than " + schema["exclusiveMinimum"].dump());
            return false;
        }
        if (schema.contains("maximum") && value > schema["maximum"].get<double>()) {
            report(ctx, "less_than_equal", "Input should be less than or equal to " + schema["maximum"].dump());
            return false;
        }
        return true;
    }

    static bool checkBool(const OJson& v, bool strict, Ctx& ctx, OJson& out) {
        if (v.is_boolean()) {
            out = v;
            return true;
        }
        if (strict) {
            report(ctx, "bool_type", "Input should be a valid boolean");
            return false;
        }
        if (v.is_number_integer() || v.is_number_float()) {
            const double d = v.get<double>();
            if (d == 0.0 || d == 1.0) {
                out = d == 1.0;
                return true;
            }
        } else if (v.is_string()) {
            std::string s = v.get<std::string>();
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            if (s == "0" || s == "off" || s == "f" || s == "false" || s == "n" || s == "no") {
                out = false;
                return true;
            }
            if (s == "1" || s == "on" || s == "t" || s == "true" || s == "y" || s == "yes") {
                out = true;
                return true;
            }
        } else {
            report(ctx, "bool_type", "Input should be a valid boolean");
            return false;
        }
        report(ctx, "bool_parsing", "Input should be a valid boolean, unable to interpret input");
        return false;
    }

    static bool checkInt(const OJson& schema, const OJson& v, bool strict, Ctx& ctx, OJson& out) {
        std::int64_t result = 0;
        if (v.is_number_integer()) {
            result = v.get<std::int64_t>();
        } else if (v.is_boolean() && !strict) {
            result = v.get<bool>() ? 1 : 0;
        } else if (v.is_number_float() && !strict) {
            if (!integral(v.get<double>(), result, ctx)) return false;
        } else if (v.is_string() && !strict) {
            const std::string s = trim(v.get<std::string>());
            const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
            if (s.empty() || ec != std::errc() || end != s.data() + s.size()) {
                double d = 0;
                if (!parseDouble(s, d)) {
                    report(ctx, "int_parsing", "Input should be a valid integer, unable to parse string as an integer");
                    return false;
                }
                if (!integral(d, result, ctx)) return false;
            }
        } else {
            report(ctx, "int_type", "Input should be a valid integer");
            return false;
        }
        if (!checkRange(schema, static_cast<double>(result), ctx)) return false;
        out = result;
        return true;
    }

    static bool integral(double d, std::int64_t& result, Ctx& ctx) {
        if (!std::isfinite(d)) {
            report(ctx, "finite_number", "Input should be a finite number");
            return false;
        }
        if (d != std::floor(d)) {
            report(ctx, "int_from_float", "Input should be a valid integer, got a number with a fractional part");
            return false;
        }
        result = static_cast<std::int64_t>(d);
        return true;
    }

    static bool checkNumber(const OJson& schema, const OJson& v, bool strict, Ctx& ctx, OJson& out) {
        double result = 0;
        if (v.is_number_integer() || v.is_number_float()) {
            result = v.get<double>();
        } else if (v.is_boolean() && !strict) {
            result = v.get<bool>() ? 1.0 : 0.0;
        } else if (v.is_string() && !strict) {
            if (!parseDouble(trim(v.get<std::string>()), result)) {
                report(ctx, "float_parsing", "Input should be a valid number, unable to parse string as a number");
                return false;
            }
        } else {
            report(ctx, "float_type", "Input should be a valid number");
            return false;
        }
        if (!checkRange(schema, result, ctx)) return false;
        out = result;
        return true;
    }
};

Validated run(const OJson& defs, const OJson& schema, const OJson& value, std::string_view model) {
    Validated result;
    Ctx ctx{&result.issues, &result.warnings, {}};
    Walker(defs).check(schema, value, false, ctx, result.value, model);
    return result;
}

}  // namespace

SchemaValidator::SchemaValidator(OJson schema) : m_schema(std::move(schema)) {
    m_rootSchema = m_schema;
    m_rootSchema.erase("$defs");
    m_rootSchema.erase("required");
    m_rootSchema["properties"]["widgets"] = {
        {"type", "object"},
        {"default", OJson::object()},
        {"additionalProperties", {{"anyOf", {{{"type", "string"}}, {{"type", "object"}}}}}},
    };

    for (auto it = m_schema["$defs"].begin(); it != m_schema["$defs"].end(); ++it) {
        const auto& def = it.value();
        if (it.key().size() < 6 || it.key().compare(it.key().size() - 5, 5, "Entry") != 0) continue;
        if (!def.contains("properties")) continue;
        const auto& props = def["properties"];
        if (!props.contains("type") || !props["type"].contains("const") || !props.contains("options") ||
            !props["options"].contains("$ref")) {
            continue;
        }
        m_optionsModelByType[props["type"]["const"].get<std::string>()] =
            props["options"]["$ref"].get<std::string>();
    }
}

const SchemaValidator& SchemaValidator::embedded() {
    static const SchemaValidator instance(OJson::parse(
        reinterpret_cast<const char*>(kEmbeddedSchema), reinterpret_cast<const char*>(kEmbeddedSchema) + kEmbeddedSchemaSize));
    return instance;
}

Validated SchemaValidator::validateRoot(const OJson& raw) const {
    return run(m_schema["$defs"], m_rootSchema, raw, "YasbConfig");
}

Validated SchemaValidator::validateWidgetOptions(std::string_view widgetType, const OJson& options) const {
    const auto it = m_optionsModelByType.find(widgetType);
    if (it == m_optionsModelByType.end()) {
        Validated failed;
        failed.issues.push_back(Issue{json::array(), "unknown_type", "Unknown widget type"});
        return failed;
    }
    const OJson ref = {{"$ref", it->second}};
    return run(m_schema["$defs"], ref, options, {});
}

bool SchemaValidator::knowsWidgetType(std::string_view widgetType) const {
    return m_optionsModelByType.find(widgetType) != m_optionsModelByType.end();
}

}  // namespace yasb::config

#pragma once

#include <QDebug>
#include <QLoggingCategory>
#include <QString>
#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/config/schema_validator.h"

Q_DECLARE_LOGGING_CATEGORY(lcConfig)

namespace yasb::config {

enum class LoadStatus {
  Ok,
  ParseError,  // invalid YAML; keep the running config
  NotFound,
  ReadError,
  ValidationError,  // root or bar errors; keep the running config
};

enum class WidgetStatus {
  Ok,
  InvalidOptions,  // drops this widget only
  UnknownType,
  MissingType,
  Undefined,  // placed but not defined (or empty)
  Error,      // definition is not a mapping
};

struct WidgetResult {
  WidgetStatus status = WidgetStatus::Undefined;
  std::string type;
  nlohmann::json options;         // normalized options, when status is Ok
  std::vector<Issue> errors;      // when status is InvalidOptions
  std::vector<std::string> bars;  // enabled bars that place the widget
};

struct LoadResult {
  LoadStatus status = LoadStatus::Ok;
  std::string message;  // parse/read failure description
  bool rootIsMapping = true;
  nlohmann::json config;              // YasbConfig.model_dump() equivalent
  std::vector<Issue> errors;          // root validation errors
  std::vector<std::string> warnings;  // deprecations, root first then widgets in placement order
  std::vector<std::string> placedWidgets;  // in placement order: enabled bars, left/center/right
  std::map<std::string, WidgetResult> widgets;

  bool ok() const { return status == LoadStatus::Ok; }
  const WidgetResult* widget(const std::string& name) const;

  // Typed options for a widget that validated, e.g. widgetOptions<options::CpuConfig>("cpu").
  // A type mismatch the schema let through is logged and yields std::nullopt, never a throw.
  template <class T>
  std::optional<T> widgetOptions(const std::string& name) const {
    const auto* w = widget(name);
    if (!w || w->status != WidgetStatus::Ok) return std::nullopt;
    try {
      return w->options.get<T>();
    } catch (const nlohmann::json::exception& error) {
      qCWarning(lcConfig).noquote() << "Widget" << QString::fromStdString(name)
                                    << "options could not be read:" << error.what();
      return std::nullopt;
    }
  }
};

using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

// Expands $env:NAME (case-insensitive, undefined becomes empty) in every string value.
OJson expandEnvironment(const OJson& value, const EnvLookup& lookup);

// Parses YAML the way PyYAML's safe_load resolves scalars (YAML 1.1 booleans, octal,
// sexagesimal...). Throws YAML::Exception on malformed input.
OJson parseYaml(std::string_view text);

LoadResult loadConfigText(std::string_view yaml, const EnvLookup& env = {});
LoadResult loadConfigFile(const std::filesystem::path& file, const EnvLookup& env = {});

// "loc.path: message" lines, for logs and dialogs.
std::string formatIssues(const std::vector<Issue>& issues);

}  // namespace yasb::config

#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yasb::config::deprecation {

// Port of core/validation/deprecation.py. Scoped tables are keyed by pydantic model class name,
// which is also the name of the model's entry under $defs in schema.json.
struct Tables {
  std::map<std::string, std::string> deprecated;
  std::map<std::string, std::pair<std::string, std::string>> renamed;  // key -> (new name, message)
  std::map<std::string, std::map<std::string, std::string>> scopedDeprecated;
  std::map<std::string, std::map<std::string, std::pair<std::string, std::string>>> scopedRenamed;
};

const Tables& tables();

// Mirrors handle_deprecated_fields: keys the model does not declare are removed or renamed in
// place, each with a warning appended in the same wording as Python's log record.
void apply(std::string_view model, nlohmann::ordered_json& object,
           const std::function<bool(const std::string&)>& isKnownField,
           std::vector<std::string>& warnings);

}  // namespace yasb::config::deprecation

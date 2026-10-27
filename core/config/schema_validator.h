#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yasb::config {

// Insertion-ordered JSON: warnings and errors follow the order of the YAML keys, like Python's
// dicts.
using OJson = nlohmann::ordered_json;

// One validation problem, shaped like a pydantic error: location, stable type id and message.
struct Issue {
  nlohmann::json loc = nlohmann::json::array();
  std::string type;
  std::string msg;
};

struct Validated {
  OJson value;
  std::vector<Issue> issues;
  std::vector<std::string> warnings;

  bool ok() const { return issues.empty(); }
};

// Validates and normalizes values against schema.json the way pydantic's lax mode does: coerces
// numeric strings and yes/no strings, rejects unknown keys, fills defaults, applies the ported
// deprecation pass per model, and runs the few validators that schema.json cannot express.
class SchemaValidator {
 public:
  explicit SchemaValidator(OJson schema);

  // The schema compiled into the binary (schema/schema.json).
  static const SchemaValidator& embedded();

  // Root and bars; `widgets` is opaque (mapping of name to string or mapping), and may be absent.
  Validated validateRoot(const OJson& raw) const;

  // Options of one widget against the `<X>Config` model its `type` selects.
  Validated validateWidgetOptions(std::string_view widgetType, const OJson& options) const;

  bool knowsWidgetType(std::string_view widgetType) const;

 private:
  OJson schema_;
  OJson rootSchema_;
  std::map<std::string, std::string, std::less<>> optionsModelByType_;
};

}  // namespace yasb::config

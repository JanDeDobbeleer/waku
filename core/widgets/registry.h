#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "core/widgets/base_widget.h"

namespace yasb::config {
struct LoadResult;
}

namespace yasb::widgets {

// What a widget factory receives: the config entry name, validated options and placement.
struct WidgetContext {
  std::string name;
  std::string type;
  nlohmann::json options;
  QString screenName;
  QString barId;
};

using WidgetFactory = std::function<BaseWidget*(const WidgetContext&)>;

// Maps config `type` strings ("yasb.cpu.CpuWidget") to factories. U8 registers each widget once at
// startup.
class WidgetRegistry {
 public:
  static WidgetRegistry& instance();

  void add(const std::string& type, WidgetFactory factory);
  bool contains(const std::string& type) const { return factories_.count(type) != 0; }

  // nullptr (with a warning) for unknown types.
  BaseWidget* create(const WidgetContext& ctx) const;

  // Builds a widget from the loaded config; nullptr when the name is undefined, invalid or
  // unregistered.
  BaseWidget* create(const config::LoadResult& config, const std::string& name,
                     const QString& screenName, const QString& barId) const;

 private:
  std::map<std::string, WidgetFactory> factories_;
};

}  // namespace yasb::widgets

#pragma once

#include <functional>
#include <map>
#include <string>

#include "core/widgets/base_widget.h"
#include "core/widgets/widget_context.h"

namespace yasb::widgets {

using WidgetFactory = std::function<BaseWidget*(const WidgetContext&)>;

// Maps config `type` strings ("yasb.cpu.CpuWidget") to factories.
class WidgetRegistry {
 public:
  void add(const std::string& type, WidgetFactory factory);
  bool contains(const std::string& type) const { return factories_.count(type) != 0; }

  // nullptr (with a warning) for unknown types.
  BaseWidget* create(const WidgetContext& ctx) const;

 private:
  std::map<std::string, WidgetFactory> factories_;
};

}  // namespace yasb::widgets

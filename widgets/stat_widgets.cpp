#include "widgets/stat_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/battery/battery_widget.h"
#include "widgets/cpu/cpu_widget.h"
#include "widgets/memory/memory_widget.h"

namespace yasb::widgets {

void registerStatWidgets() {
  static bool registered = false;
  if (registered) return;
  registered = true;
  auto& registry = WidgetRegistry::instance();
  registry.add("yasb.cpu.CpuWidget",
               [](const WidgetContext& context) { return new CpuWidget(context); });
  registry.add("yasb.memory.MemoryWidget",
               [](const WidgetContext& context) { return new MemoryWidget(context); });
  registry.add("yasb.battery.BatteryWidget",
               [](const WidgetContext& context) { return new BatteryWidget(context); });
}

}  // namespace yasb::widgets

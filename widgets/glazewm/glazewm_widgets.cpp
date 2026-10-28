#include "widgets/glazewm/glazewm_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/glazewm/tiling_direction_widget.h"

namespace yasb::widgets {

void registerGlazewmWidgets(WidgetRegistry& registry) {
  registry.add(
      "glazewm.tiling_direction.GlazewmTilingDirectionWidget",
      [](const WidgetContext& context) { return new GlazewmTilingDirectionWidget(context); });
}

}  // namespace yasb::widgets

#include "widgets/glazewm/glazewm_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/glazewm/tiling_direction_widget.h"
#include "widgets/glazewm/workspaces_widget.h"

namespace yasb::widgets {

void registerGlazewmWidgets(WidgetRegistry& registry) {
  registry.add(
      "glazewm.tiling_direction.GlazewmTilingDirectionWidget",
      [](const WidgetContext& context) { return new GlazewmTilingDirectionWidget(context); });
  registry.add("glazewm.workspaces.GlazewmWorkspacesWidget",
               [](const WidgetContext& context) { return new GlazewmWorkspacesWidget(context); });
}

}  // namespace yasb::widgets

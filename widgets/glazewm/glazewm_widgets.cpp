#include "widgets/glazewm/glazewm_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/glazewm/binding_mode_widget.h"

namespace yasb::widgets {

void registerGlazewmWidgets(WidgetRegistry& registry) {
  registry.add("glazewm.binding_mode.GlazewmBindingModeWidget",
               [](const WidgetContext& context) { return new GlazewmBindingModeWidget(context); });
}

}  // namespace yasb::widgets

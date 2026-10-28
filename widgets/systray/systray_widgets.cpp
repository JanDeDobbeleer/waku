#include "widgets/systray/systray_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/systray/systray_widget.h"

namespace yasb::widgets {

void registerSystrayWidgets(WidgetRegistry& registry) {
  registry.add("yasb.systray.SystrayWidget",
               [](const WidgetContext& context) { return new SystrayWidget(context); });
}

}  // namespace yasb::widgets

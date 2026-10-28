#include "widgets/komorebi/komorebi_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/komorebi/active_layout_widget.h"
#include "widgets/komorebi/workspaces_widget.h"

namespace yasb::widgets {

void registerKomorebiWidgets(WidgetRegistry& registry) {
  registry.add("komorebi.workspaces.WorkspaceWidget",
               [](const WidgetContext& context) { return new KomorebiWorkspacesWidget(context); });
  registry.add("komorebi.active_layout.ActiveLayoutWidget", [](const WidgetContext& context) {
    return new KomorebiActiveLayoutWidget(context);
  });
}

}  // namespace yasb::widgets

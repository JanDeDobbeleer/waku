#pragma once

namespace yasb::widgets {

class WidgetRegistry;

// Registers the komorebi widget factories ("komorebi.workspaces.WorkspaceWidget",
// "komorebi.active_layout.ActiveLayoutWidget").
void registerKomorebiWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets

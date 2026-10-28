#pragma once

namespace yasb::widgets {

class WidgetRegistry;

// Registers the GlazeWM widget factories (workspaces, binding mode, tiling direction). The widgets
// arrive in later steps; nothing is registered yet.
void registerGlazewmWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets

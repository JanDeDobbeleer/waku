#pragma once

namespace yasb::widgets {

class WidgetRegistry;

// Registers the systray widget factory ("yasb.systray.SystrayWidget").
void registerSystrayWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets

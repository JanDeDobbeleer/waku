#pragma once

namespace yasb::widgets {

class WidgetRegistry;

// Registers the Wi-Fi and Bluetooth widget factories in the application's registry.
void registerNetworkWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets

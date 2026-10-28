#include "widgets/network_widgets.h"

#include "core/widgets/registry.h"
#include "widgets/bluetooth/bluetooth_widget.h"
#include "widgets/wifi/wifi_widget.h"

namespace yasb::widgets {

void registerNetworkWidgets(WidgetRegistry& registry) {
  registry.add("yasb.wifi.WifiWidget",
               [](const WidgetContext& context) { return new WifiWidget(context); });
  registry.add("yasb.bluetooth.BluetoothWidget",
               [](const WidgetContext& context) { return new BluetoothWidget(context); });
}

}  // namespace yasb::widgets

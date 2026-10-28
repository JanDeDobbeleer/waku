#pragma once

#include <QLabel>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/connectivity.h"
#include "platform/wlan.h"

namespace yasb::widgets {

class WifiMenu;

// Port of yasb.wifi.WifiWidget: icon/name labels for Wi-Fi, an Ethernet label group, and the VPN
// badge, plus the network menu (toggle_menu).
class WifiWidget final : public BaseWidget {
  Q_OBJECT
 public:
  using BackendFactory = std::function<std::unique_ptr<platform::WifiBackend>()>;
  using IpResolver = std::function<std::optional<std::string>()>;

  // `startPolling` false builds the widget without a worker (tests feed samples with applySample).
  // `backendFactory` creates the Wi-Fi backend the first time the menu opens; null uses wlanapi.
  explicit WifiWidget(const WidgetContext& context, bool startPolling = true,
                      BackendFactory backendFactory = {}, IpResolver ipResolver = {});
  ~WifiWidget() override;

  // One poll result (WifiWidget._on_wifi_info_result).
  void applySample(const platform::ConnectivitySample& sample);

  const std::vector<QLabel*>& ethernetLabels() const { return ethernetLabels_; }
  const std::vector<QLabel*>& ethernetLabelsAlt() const { return ethernetLabelsAlt_; }
  bool ethernetActive() const { return ethernetActive_; }
  WifiMenu* menu() const { return menu_; }

 private:
  void render();
  void displayCorrectLabel();
  void toggleLabel();
  QString wifiIcon(int strength) const;
  std::vector<QLabel*> buildLabels(const QString& content, bool isAlt, bool isEthernet);

  config::options::WifiConfig options_;
  std::vector<QLabel*> ethernetLabels_;
  std::vector<QLabel*> ethernetLabelsAlt_;
  bool ethernetActive_ = false;
  bool showAltLabel_ = false;
  platform::WifiInfo info_{0, "Disconnected", -1, false, {}};
  // A failed lookup (nullopt) takes Python's error path.
  std::optional<std::string> ip_;
  IpResolver ipResolver_;
  WifiMenu* menu_ = nullptr;
};

}  // namespace yasb::widgets

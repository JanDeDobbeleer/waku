#pragma once

#include <QStringList>
#include <memory>
#include <optional>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/bluetooth.h"

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

class BluetoothMenu;

// Port of yasb.bluetooth.BluetoothWidget: radio/device icon and connected device names on the bar,
// plus the device menu (toggle_menu). Widgets of one runtime share one backend.
class BluetoothWidget final : public BaseWidget {
  Q_OBJECT
 public:
  // `backend` null acquires the shared real backend (created on first use) and starts it.
  explicit BluetoothWidget(const WidgetContext& context,
                           std::shared_ptr<platform::BluetoothBackend> backend = nullptr);
  ~BluetoothWidget() override;

  // BluetoothWidget._on_status_updated / _update_bar.
  void applyStatus(const platform::BluetoothStatus& status);
  const QString& state() const { return state_; }
  BluetoothMenu* menu() const { return menu_; }

 private:
  void toggleLabel();
  void updateLabel(const QString& icon, const std::optional<QStringList>& connected);
  void setStateClass(QLabel* label);

  config::options::BluetoothConfig options_;
  bar::GlobalState& globalState_;
  std::shared_ptr<platform::BluetoothBackend> backend_;
  BluetoothMenu* menu_ = nullptr;
  QString icon_;
  std::optional<QStringList> connectedDevices_;
  QString state_ = QStringLiteral("bt-off");
};

}  // namespace yasb::widgets

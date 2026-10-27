#pragma once

#include <QPointer>
#include <optional>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/system_stats.h"

namespace yasb::widgets {

class BatteryWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit BatteryWidget(const WidgetContext& context, bool startPolling = true);
  void applySample(const std::optional<platform::BatterySample>& sample);

 private:
  bool event(QEvent* event) override;
  void render();
  void toggleLabel();
  QString threshold(int percent) const;
  QString timeRemaining(int seconds) const;
  QString chargingIcon(const QString& threshold) const;

  config::options::BatteryConfig options_;
  QString label_;
  QString labelAlt_;
  bool hideUnsupported_ = true;
  bool naturalTime_ = false;
  std::optional<platform::BatterySample> last_;
};

}  // namespace yasb::widgets

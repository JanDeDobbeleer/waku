#pragma once

#include <QPointer>
#include <deque>
#include <optional>

#include "core/bar/theme_class.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/stat_popup.h"
#include "platform/system_stats.h"

namespace yasb::widgets {

class CpuWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit CpuWidget(const WidgetContext& context, bool startPolling = true);
  void applySample(const platform::CpuSample& sample);

 private:
  void render();
  void toggleLabel();
  void toggleMenu();
  void updatePopup();
  QString histogram(double value, double minimum, double maximum) const;
  QString threshold(double value) const;
  void openPopup();

  nlohmann::json options_;
  bar::GlobalState& globalState_;
  QString label_;
  QString labelAlt_;
  bool hideDecimal_ = false;
  int historyLimit_ = 60;
  std::optional<platform::CpuSample> last_;
  std::deque<double> frequencyHistory_;
  std::deque<double> percentHistory_;
  std::deque<double> graphHistory_;
  QPointer<PinnablePopup> popup_;
};

}  // namespace yasb::widgets

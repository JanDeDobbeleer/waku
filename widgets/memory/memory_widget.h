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

class MemoryWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit MemoryWidget(const WidgetContext& context, bool startPolling = true);
  void applySample(const platform::MemorySample& sample);

 private:
  void render();
  void toggleLabel();
  void toggleMenu();
  void updatePopup();
  void openPopup();
  QString threshold(double value) const;

  nlohmann::json options_;
  bar::GlobalState& globalState_;
  QString label_;
  QString labelAlt_;
  bool hideDecimal_ = false;
  int historyLimit_ = 60;
  std::optional<platform::MemorySample> last_;
  std::deque<double> graphHistory_;
  QPointer<PinnablePopup> popup_;
};

}  // namespace yasb::widgets

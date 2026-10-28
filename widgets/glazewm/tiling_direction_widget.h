#pragma once

#include "core/config/options/glazewm_tiling_direction.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/widget_context.h"
#include "widgets/glazewm/glazewm_state.h"

class QPushButton;

namespace yasb::widgets {

// Port of glazewm.tiling_direction.GlazewmTilingDirectionWidget: one button showing the tiling
// direction of the focused container; a click toggles it. Hidden while GlazeWM is not connected.
//
// Threading: GUI thread only. State arrives as "glazewm" EventBus events from GlazewmService,
// commands leave as "glazewm_command" events.
class GlazewmTilingDirectionWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit GlazewmTilingDirectionWidget(const WidgetContext& context, bool subscribe = true);

  void onConnection(bool connected);
  void onTilingDirection(glazewm::TilingDirection direction);
  void toggleTilingDirection();

  QPushButton* button() const { return button_; }

 private:
  void onBusEvent(const QString& name, const QVariantMap& payload);

  EventBus& bus_;
  config::options::GlazewmTilingDirectionConfig options_;
  QPushButton* button_ = nullptr;
  glazewm::TilingDirection direction_ = glazewm::TilingDirection::Horizontal;
};

}  // namespace yasb::widgets

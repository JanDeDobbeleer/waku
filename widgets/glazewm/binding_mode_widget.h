#pragma once

#include <optional>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/widget_context.h"
#include "widgets/glazewm/glazewm_state.h"

class QEvent;

namespace yasb::widgets {

// Port of glazewm.binding_mode.GlazewmBindingModeWidget: shows the active GlazeWM binding mode and
// cycles through the configured modes.
//
// Threading: GUI thread only. State arrives as "glazewm" EventBus events from GlazewmService
// (protocol in widgets/glazewm/glazewm_service.h), commands leave as "glazewm_command" events.
class GlazewmBindingModeWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit GlazewmBindingModeWidget(const WidgetContext& context, bool subscribe = true);

  // What the service's "connection" and "binding_mode" events call.
  void onConnectionStatus(bool connected);
  void onBindingMode(const glazewm::BindingMode& mode);

 protected:
  bool event(QEvent* event) override;

 private:
  void onBusEvent(const QString& name, const QVariantMap& payload);
  void setShown(bool shown);
  void toggleLabel();
  void updateLabel();
  void disableBindingMode();
  void cycleThroughBindingModes(int direction);
  bool activeHasName() const { return active_ && active_->name && !active_->name->isEmpty(); }

  EventBus& bus_;
  config::options::GlazewmBindingModeConfig options_;
  QString label_;
  QString labelAlt_;
  std::vector<QString> cycle_;
  int cycleIndex_ = 0;
  // Python leaves _active_binding_mode unset until the first shown event; so does this.
  std::optional<glazewm::BindingMode> active_;
  bool wantShown_ = false;
};

}  // namespace yasb::widgets

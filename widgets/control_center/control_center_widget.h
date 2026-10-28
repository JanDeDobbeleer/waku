#pragma once

#include <QPointer>
#include <map>
#include <string>

#include "core/bar/theme_class.h"
#include "core/config/options/control_center.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "widgets/control_center/control_center_sections.h"
#include "widgets/control_center/control_center_system.h"
#include "widgets/menu_support/menu_popup.h"

namespace yasb::widgets {

// Port of yasb.control_center.ControlCenterWidget: a bar icon that opens a popup of configurable
// sections (system controls, quick actions, sliders, power, media). The screenshot editor (U19) is
// not part of this port; the screenshot quick action opens the system snipping overlay instead.
//
// Threading: GUI thread only. The audio, brightness and media services deliver their change
// notifications to this thread; blocking system calls go through `ControlCenterSystem`.
class ControlCenterWidget final : public BaseWidget {
  Q_OBJECT
 public:
  ControlCenterWidget(const WidgetContext& context, ControlCenterSystem system);

  // toggle_menu: opens the popup, or fades it out when it is already open.
  void toggleMenu();
  void showMenu();
  // Re-reads the system into the open popup's sections (a no-op while it is closed).
  void refreshPopupState();

  MenuPopup* dialog() const { return dialog_; }
  bool dialogVisible() const { return dialog_ && dialog_->isVisible(); }
  // The built sections (null when hidden by config or before the first open).
  SystemControlsSection* systemControls() const { return systemControls_; }
  QuickActionsSection* quickActions() const { return quickActions_; }
  SlidersSection* sliders() const { return sliders_; }
  PowerSection* power() const { return power_; }
  MediaSection* media() const { return media_; }
  const ControlCenterSystem& system() const { return system_; }

 private:
  void refreshAudioState();
  void onBrightnessChanged(qulonglong monitor, int percent);
  SectionContext sectionContext();

  config::options::ControlCenterConfig options_;
  ControlCenterSystem system_;
  bar::GlobalState& globalState_;
  QPointer<MenuPopup> dialog_;
  QPointer<SystemControlsSection> systemControls_;
  QPointer<QuickActionsSection> quickActions_;
  QPointer<SlidersSection> sliders_;
  QPointer<PowerSection> power_;
  QPointer<MediaSection> media_;
};

// Registers "yasb.control_center.ControlCenterWidget".
void registerControlCenterWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets

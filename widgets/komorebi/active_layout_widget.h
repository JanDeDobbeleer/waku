#pragma once

#include <QPointer>
#include <deque>
#include <functional>
#include <optional>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/widget_context.h"
#include "widgets/komorebi/komorebi_service.h"
#include "widgets/menu_support/menu_popup.h"

class QEvent;
class QLabel;

namespace yasb::widgets {

// Port of komorebi.active_layout.ActiveLayoutWidget: layout icon/name of the focused workspace on
// this bar's monitor, layout cycling over the configured list, toggles and the layout menu.
//
// Threading: GUI thread only. State arrives as "komorebi" EventBus events from KomorebiService,
// commands leave as "komorebi_command" events.
class KomorebiActiveLayoutWidget final : public BaseWidget {
  Q_OBJECT
 public:
  using MonitorResolver = std::function<std::optional<long long>()>;

  explicit KomorebiActiveLayoutWidget(const WidgetContext& context, bool subscribe = true);

  void setMonitorResolver(MonitorResolver resolver) { monitorResolver_ = std::move(resolver); }

  void onConnect(komorebi::JsonPtr state);
  void onUpdate(komorebi::JsonPtr event, komorebi::JsonPtr state);
  void onDisconnect();

  QLabel* label() const { return text_; }
  // Front of the cycling list (what Python's `_layouts[0]` holds), e.g. "bsp".
  QString currentLayoutCommand() const { return layouts_.empty() ? QString() : layouts_.front(); }

 protected:
  bool event(QEvent* event) override;

 private:
  using Json = nlohmann::json;

  std::optional<long long> resolveMonitor() const;
  void notePending(const komorebi::JsonPtr& state);
  void onBusEvent(const QString& name, const QVariantMap& payload);
  void resetLayouts();
  bool updateState(const komorebi::JsonPtr& state);
  void updateActiveLayout(const komorebi::JsonPtr& state, bool isConnect);
  bool labelInfo(QString* name, QString* icon) const;
  std::string iconFor(const std::string& snakeName) const;
  bool shiftAllowed() const;
  void nextLayout();
  void prevLayout();
  void firstLayout();
  void toggleBlockingState();
  void changeLayout(const QString& layout);
  void toggle(const QString& what);
  void flip(const QString& direction);
  void showLayoutMenu();
  bool isPaused() const;
  bool workspaceFlag(const char* key) const;

  EventBus& bus_;
  config::options::ActiveLayoutConfig options_;
  MonitorResolver monitorResolver_;
  QLabel* text_ = nullptr;
  std::deque<QString> layouts_;
  komorebi::JsonPtr state_;
  komorebi::JsonPtr pending_;  // dropped for want of a window; replayed once there is one
  komorebi::JsonPtr owner_;    // keeps screen_/workspace_ alive across failed updates
  const Json* screen_ = nullptr;
  int screenIndex_ = 0;
  const Json* workspace_ = nullptr;
  int workspaceIndex_ = 0;
  QPointer<MenuPopup> menu_;
};

}  // namespace yasb::widgets

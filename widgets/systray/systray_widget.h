#pragma once

#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <optional>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/systray_host.h"
#include "platform/systray_types.h"
#include "widgets/systray/systray_popup.h"
#include "widgets/systray/systray_state.h"
#include "widgets/systray/tray_drop_widget.h"
#include "widgets/systray/tray_icon_button.h"

namespace yasb::widgets {

// What a systray widget needs from the outside. Production leaves it empty and the widget takes
// the shared `SystrayService` from the polling-worker registry; tests pass fakes so nothing
// touches the real tray, the real windows or the user's saved state.
struct SystrayDeps {
  platform::TrayHost* host = nullptr;            // not owned; null: no icons arrive
  platform::TrayWindowApi* windowApi = nullptr;  // not owned
  bool connectRefresh = true;                    // answer the host's "re-announce icons" request
  QString stateDir;                              // empty: %LOCALAPPDATA%\YASB
  std::optional<QString> screenId;               // overrides the screen identity
};

// Port of yasb.systray.SystrayWidget: tray icons in a pinned container on the bar and an unpinned
// container (inline, or a popup grid with show_in_popup). <modifier>+click pins or unpins, icons
// drag between the containers, and pin state and order persist per screen.
class SystrayWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit SystrayWidget(const WidgetContext& context, std::optional<SystrayDeps> deps = {});
  ~SystrayWidget() override;

  // --- host signals (public so tests can feed them directly) ---
  void onIconModified(const platform::TrayIconData& data);
  void onIconDeleted(const platform::TrayIconData& data);

  void refreshSystray();
  void checkIcons();
  void sortIcons();
  void saveState();
  void loadState();
  void updatePinnedWidgetVisibility(bool forceShow = false);
  void toggleUnpinnedWidgetVisibility();
  void setContainersVisibility();

  // --- inspection ---
  const std::vector<QPointer<TrayIconButton>>& icons() const { return icons_; }
  TrayDropWidget* pinnedWidget() const { return pinnedWidget_; }
  // The inline unpinned container; null in popup mode.
  TrayDropWidget* unpinnedWidget() const { return unpinnedWidget_; }
  SystrayPopup* popup() const { return popup_; }
  QPushButton* visibilityButton() const { return visibilityButton_; }
  QLayout* pinnedLayout() const { return pinnedWidget_->mainLayout(); }
  QLayout* unpinnedLayout() const { return unpinnedLayout_; }
  const IconStateMap& currentState() const { return currentState_; }
  void setCurrentState(const IconStateMap& state) { currentState_ = state; }
  QString stateFilePath() const;
  bool refreshTimerActive() const { return refreshTimer_.isActive(); }

 private:
  void setupClient();
  void showContextMenu(const QPoint& position);
  void onDragStarted(int iconWidth, bool fromPinned);
  void onDragEnded();
  void onIconPinnedChanged(TrayIconButton* icon);
  void onIconMoved(TrayIconButton* icon);
  bool isHiddenIcon(const platform::TrayIconData& data) const;
  TrayIconButton* findIcon(const std::optional<QUuid>& guid, std::uint64_t hwnd,
                           std::uint32_t uid) const;
  static bool isLayoutEmpty(QLayout* layout);
  std::vector<TrayIconButton*> iconsInLayout(QLayout* layout) const;
  QStringList visibleStateKeys(QLayout* layout) const;
  void updateCurrentState();
  QString screenId();
  void addIconToUnpinned(TrayIconButton* icon);
  void relayoutPopupGrid();
  void removeFromLayout(TrayIconButton* icon);
  void applyIconData(TrayIconButton* icon, const platform::TrayIconData& data);

  config::options::SystrayWidgetConfig options_;
  SystrayDeps deps_;
  TrayIconSettings iconSettings_;
  bar::GlobalState* globalState_;
  QSet<QUuid> filteredGuids_;
  QStringList hiddenIconsLower_;
  std::vector<QPointer<TrayIconButton>> icons_;
  IconStateMap currentState_;
  std::optional<QString> screenId_;

  QTimer iconCheckTimer_;
  QTimer refreshTimer_;
  QTimer sortTimer_;
  QTimer pinnedVisibilityTimer_;

  QPushButton* visibilityButton_ = nullptr;
  TrayDropWidget* unpinnedWidget_ = nullptr;
  TrayDropWidget* pinnedWidget_ = nullptr;
  SystrayPopup* popup_ = nullptr;
  QLayout* unpinnedLayout_ = nullptr;
};

}  // namespace yasb::widgets

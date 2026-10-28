#pragma once

#include <QPushButton>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/widget_context.h"
#include "widgets/komorebi/komorebi_service.h"

class QLabel;

namespace yasb::widgets {

enum class WorkspaceStatus { Empty, Populated, Active };

// Port of WorkspaceButton: class "ws-btn <status> [button-N]", label per status.
class WorkspaceButton final : public QPushButton {
  Q_OBJECT
 public:
  WorkspaceButton(int workspaceIndex, const QString& defaultLabel, const QString& activeLabel,
                  const QString& populatedLabel, QWidget* parent);

  int workspaceIndex() const { return workspaceIndex_; }
  WorkspaceStatus status() const { return status_; }
  void setLabels(const QString& defaultLabel, const QString& activeLabel,
                 const QString& populatedLabel);
  const QString& defaultLabel() const { return defaultLabel_; }
  const QString& activeLabel() const { return activeLabel_; }
  const QString& populatedLabel() const { return populatedLabel_; }
  void updateAndRedraw(WorkspaceStatus status);
  void setClass(const QString& cssClass);

 private:
  int workspaceIndex_;
  WorkspaceStatus status_ = WorkspaceStatus::Empty;
  QString defaultLabel_;
  QString activeLabel_;
  QString populatedLabel_;
};

// Port of komorebi.workspaces.WorkspaceWidget (plain buttons; app icons are not ported).
//
// Threading: GUI thread only. Komorebi data arrives as "komorebi" EventBus events published by
// KomorebiService; commands leave as "komorebi_command" events.
class KomorebiWorkspacesWidget final : public BaseWidget {
  Q_OBJECT
 public:
  // Which monitor (komorebi monitor "id", an HMONITOR) the bar window hosting this widget is on.
  using MonitorResolver = std::function<std::optional<long long>()>;

  explicit KomorebiWorkspacesWidget(const WidgetContext& context, bool subscribe = true);

  void setMonitorResolver(MonitorResolver resolver) { monitorResolver_ = std::move(resolver); }

  // Entry points for the three komorebi notifications (also used directly by tests).
  void onConnect(komorebi::JsonPtr state);
  void onUpdate(komorebi::JsonPtr event, komorebi::JsonPtr state);
  void onDisconnect();
  // Wheel handling with a QWheelEvent::angleDelta().y() value.
  void scrollWorkspaces(int deltaY);

  const std::vector<WorkspaceButton*>& buttons() const { return buttons_; }
  std::optional<int> currentWorkspace() const { return current_; }
  QLabel* offlineLabel() const { return offline_; }
  QLabel* floatOverrideLabel() const { return floatOverride_; }
  QLabel* workspaceLayerLabel() const { return layerLabel_; }

 protected:
  void wheelEvent(QWheelEvent* event) override;

 private:
  using Json = nlohmann::json;

  struct Labels {
    QString normal;
    QString active;
    QString populated;
  };

  void onBusEvent(const QString& name, const QVariantMap& payload);
  void reset();
  bool updateState(const komorebi::JsonPtr& state);
  bool indexChanged() const { return previous_ != current_; }
  void updateButton(WorkspaceButton* button);
  void refreshButtonLabels(WorkspaceButton* button);
  std::optional<Labels> workspaceLabels(int workspaceIndex) const;
  void addOrUpdateButtons();
  WorkspaceButton* tryAddButton(int workspaceIndex);
  void hideUnknownButtons();
  void updateVisibleButtons();
  void updateWorkspaceLayer(std::optional<int> workspaceIndex);
  void showOffline();
  void hideOffline();
  void clearContainer();
  void activateWorkspace(int workspaceIndex);
  WorkspaceButton* buttonAt(std::optional<int> index) const;
  void sendWorkspaceUpdate(const QString& eventType);

  EventBus& bus_;
  config::options::KomorebiWorkspacesConfig options_;
  MonitorResolver monitorResolver_;
  QLabel* offline_ = nullptr;
  QLabel* floatOverride_ = nullptr;
  QLabel* layerLabel_ = nullptr;
  std::vector<WorkspaceButton*> buttons_;

  komorebi::JsonPtr state_;
  const Json* screen_ = nullptr;  // points into state_
  int screenIndex_ = 0;
  int workspaceCount_ = 0;
  std::optional<int> previous_;
  std::optional<int> current_;
  std::vector<int> previousWindows_;
  std::vector<int> currentWindows_;
};

}  // namespace yasb::widgets

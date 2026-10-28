#pragma once

#include <QFrame>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/widget_context.h"
#include "platform/window_system.h"
#include "widgets/komorebi/komorebi_service.h"

class QHBoxLayout;
class QLabel;

namespace yasb::widgets {

class TaskbarIcons;

enum class WorkspaceStatus { Empty, Populated, Active };

// What the workspaces widget needs from either button flavour (Python duck-types WorkspaceButton
// and WorkspaceButtonWithIcons). Both carry class "ws-btn <status> [button-N]" and a label per
// status.
class WorkspaceButtonBase {
 public:
  virtual ~WorkspaceButtonBase() = default;

  virtual QWidget* widget() = 0;
  const QWidget* widget() const { return const_cast<WorkspaceButtonBase*>(this)->widget(); }
  // The visible label text of the button.
  virtual QString labelText() const = 0;
  // Runs the click action (a click on a plain button, a left press on an icon button).
  virtual void activate() = 0;
  virtual void updateAndRedraw(WorkspaceStatus status) = 0;

  int workspaceIndex() const { return workspaceIndex_; }
  WorkspaceStatus status() const { return status_; }
  void setLabels(const QString& defaultLabel, const QString& activeLabel,
                 const QString& populatedLabel);
  const QString& defaultLabel() const { return defaultLabel_; }
  const QString& activeLabel() const { return activeLabel_; }
  const QString& populatedLabel() const { return populatedLabel_; }
  void setClass(const QString& cssClass);
  void setActivateHandler(std::function<void()> handler) { onActivate_ = std::move(handler); }

 protected:
  WorkspaceButtonBase(int workspaceIndex, const QString& defaultLabel, const QString& activeLabel,
                      const QString& populatedLabel);
  void setStatusClass(WorkspaceStatus status);
  const QString& labelFor(WorkspaceStatus status) const;
  void runActivate() const {
    if (onActivate_) onActivate_();
  }

  int workspaceIndex_;
  WorkspaceStatus status_ = WorkspaceStatus::Empty;
  QString defaultLabel_;
  QString activeLabel_;
  QString populatedLabel_;

 private:
  std::function<void()> onActivate_;
};

// Port of WorkspaceButton.
class WorkspaceButton final : public QPushButton, public WorkspaceButtonBase {
  Q_OBJECT
 public:
  WorkspaceButton(int workspaceIndex, const QString& defaultLabel, const QString& activeLabel,
                  const QString& populatedLabel, QWidget* parent);

  QWidget* widget() override { return this; }
  QString labelText() const override { return text(); }
  void activate() override { click(); }
  void updateAndRedraw(WorkspaceStatus status) override;
};

// hwnd -> pixmap in insertion order (Python dict[int, QPixmap | None]); a null pixmap is None.
using WorkspaceIcons = std::vector<std::pair<long long, QPixmap>>;

// Port of WorkspaceButtonWithIcons: a QFrame with a "label" QLabel and one "icon icon-N" QLabel
// per app icon. A left press activates the workspace.
class WorkspaceIconButton final : public QFrame, public WorkspaceButtonBase {
  Q_OBJECT
 public:
  using IconSource = std::function<WorkspaceIcons(int workspaceIndex)>;
  using IconLookup = std::function<QPixmap(long long hwnd)>;
  using CurrentIndex = std::function<std::optional<int>()>;

  // `allIcons` gathers every window icon of the workspace (parent.get_all_icons_in_workspace);
  // `oneIcon` resolves one window (parent.get_app_icon).
  WorkspaceIconButton(int workspaceIndex, const QString& defaultLabel, const QString& activeLabel,
                      const QString& populatedLabel,
                      const config::options::KomorebiAppIconsConfig& options, IconSource allIcons,
                      IconLookup oneIcon, CurrentIndex currentIndex, QWidget* parent);

  QWidget* widget() override { return this; }
  QString labelText() const override;
  void activate() override { runActivate(); }
  void updateAndRedraw(WorkspaceStatus status) override;

  // update_icons(): `icons` null reloads every icon of the workspace, else merges it in.
  void updateIcons(const WorkspaceIcons* icons = nullptr);
  void updateIconByHwnd(long long hwnd);
  const WorkspaceIcons& icons() const { return icons_; }
  const std::vector<QLabel*>& iconLabels() const { return iconLabels_; }
  QLabel* textLabel() const { return text_; }

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  config::options::KomorebiAppIconsConfig options_;
  IconSource allIcons_;
  IconLookup oneIcon_;
  CurrentIndex currentIndex_;
  QHBoxLayout* layout_ = nullptr;
  QLabel* text_ = nullptr;
  WorkspaceIcons icons_;
  std::vector<QLabel*> iconLabels_;
};

// Port of komorebi.workspaces.WorkspaceWidget. With `app_icons` enabled the buttons are
// WorkspaceIconButtons showing the icons of the workspace's windows.
//
// Threading: GUI thread only. Komorebi data arrives as "komorebi" EventBus events published by
// KomorebiService; commands leave as "komorebi_command" events. Window icons load on the
// TaskbarIcons worker thread and arrive through its iconsChanged signal.
class KomorebiWorkspacesWidget final : public BaseWidget {
  Q_OBJECT
 public:
  // Which monitor (komorebi monitor "id", an HMONITOR) the bar window hosting this widget is on.
  using MonitorResolver = std::function<std::optional<long long>()>;

  explicit KomorebiWorkspacesWidget(const WidgetContext& context, bool subscribe = true);
  ~KomorebiWorkspacesWidget() override;

  void setMonitorResolver(MonitorResolver resolver) { monitorResolver_ = std::move(resolver); }
  // Replaces the window system the app icons come from (tests); `async` false loads inline.
  void setWindowSystem(std::shared_ptr<platform::WindowSystem> system, bool async = false);

  // Entry points for the three komorebi notifications (also used directly by tests).
  void onConnect(komorebi::JsonPtr state);
  void onUpdate(komorebi::JsonPtr event, komorebi::JsonPtr state);
  void onDisconnect();
  // Wheel handling with a QWheelEvent::angleDelta().y() value.
  void scrollWorkspaces(int deltaY);

  const std::vector<WorkspaceButtonBase*>& buttons() const { return buttons_; }
  std::optional<int> currentWorkspace() const { return current_; }
  QLabel* offlineLabel() const { return offline_; }
  QLabel* floatOverrideLabel() const { return floatOverride_; }
  QLabel* workspaceLayerLabel() const { return layerLabel_; }
  // True while a connect/update was dropped because the widget has no window (and so no monitor)
  // yet; it is replayed as soon as the widget is parented or shown.
  bool hasPendingState() const { return pending_ != nullptr; }

 protected:
  void wheelEvent(QWheelEvent* event) override;
  bool event(QEvent* event) override;

 private:
  using Json = nlohmann::json;

  struct Labels {
    QString normal;
    QString active;
    QString populated;
  };

  void onBusEvent(const QString& name, const QVariantMap& payload);
  void reset();
  std::optional<long long> resolveMonitor() const;
  bool updateState(const komorebi::JsonPtr& state);
  bool indexChanged() const { return previous_ != current_; }
  void updateButton(WorkspaceButtonBase* button);
  void refreshButtonLabels(WorkspaceButtonBase* button);
  std::optional<Labels> workspaceLabels(int workspaceIndex) const;
  void addOrUpdateButtons();
  WorkspaceButtonBase* tryAddButton(int workspaceIndex);
  void hideUnknownButtons();
  void updateVisibleButtons();
  void updateWorkspaceLayer(std::optional<int> workspaceIndex);
  void showOffline();
  void hideOffline();
  void clearContainer();
  void activateWorkspace(int workspaceIndex);
  WorkspaceButtonBase* buttonAt(std::optional<int> index) const;
  WorkspaceIconButton* iconButtonAt(std::optional<int> index) const;
  void sendWorkspaceUpdate(const QString& eventType);
  void retryPending();

  // --- app icons
  bool appIconsEnabled() const;
  void updateIconsForEvent(const std::string& type, const Json* event);
  WorkspaceIcons allIconsInWorkspace(int workspaceIndex);
  QPixmap appIcon(long long hwnd);
  void refreshIcons();

  EventBus& bus_;
  config::options::KomorebiWorkspacesConfig options_;
  MonitorResolver monitorResolver_;
  QLabel* offline_ = nullptr;
  QLabel* floatOverride_ = nullptr;
  QLabel* layerLabel_ = nullptr;
  std::vector<WorkspaceButtonBase*> buttons_;

  komorebi::JsonPtr state_;
  komorebi::JsonPtr pending_;
  const Json* screen_ = nullptr;  // points into state_
  int screenIndex_ = 0;
  int workspaceCount_ = 0;
  std::optional<int> previous_;
  std::optional<int> current_;
  std::vector<int> previousWindows_;
  std::vector<int> currentWindows_;

  std::shared_ptr<platform::WindowSystem> windowSystem_;
  TaskbarIcons* icons_ = nullptr;
  std::set<std::uint32_t> uniquePids_;
};

}  // namespace yasb::widgets

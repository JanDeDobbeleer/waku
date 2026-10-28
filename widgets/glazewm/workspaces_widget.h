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
#include "widgets/glazewm/glazewm_service.h"

class QHBoxLayout;
class QLabel;
class QScreen;

namespace yasb::widgets {

class TaskbarIcons;

// Port of WorkspaceStatus (glazewm/workspaces.py).
enum class GlazewmWorkspaceStatus {
  Empty,
  Populated,
  ActiveEmpty,
  ActivePopulated,
  FocusedEmpty,
  FocusedPopulated
};

// The StrEnum value of a status ("active_populated").
QString glazewmStatusName(GlazewmWorkspaceStatus status);

// Python natural_sort_key(a) < natural_sort_key(b): digit runs compare numerically, text lowercase.
bool glazewmNaturalLess(const QString& a, const QString& b);

// What the workspaces widget needs from either button flavour (Python duck-types
// GlazewmWorkspaceButton and GlazewmWorkspaceButtonWithIcons).
class GlazewmWorkspaceButtonBase {
 public:
  virtual ~GlazewmWorkspaceButtonBase() = default;

  virtual QWidget* widget() = 0;
  const QWidget* widget() const { return const_cast<GlazewmWorkspaceButtonBase*>(this)->widget(); }
  // The visible label text of the button.
  virtual QString labelText() const = 0;
  // Runs the click action (a click on a plain button, a left press on an icon button).
  virtual void activate() = 0;

  // update_button(): status, label, icons and the CSS class. Throws FormatError like Python's
  // KeyError/ValueError from str.format_map on a bad label template.
  void updateButton();
  GlazewmWorkspaceStatus status() const { return status_; }
  void setActivateHandler(std::function<void()> handler) { onActivate_ = std::move(handler); }

  // The mutable button model the widget assigns before each updateButton().
  QString name;
  QString displayName;
  bool monitorExclusive = false;
  bool isDisplayed = false;
  bool isFocused = false;
  int windowCount = 0;

 protected:
  explicit GlazewmWorkspaceButtonBase(const config::options::GlazewmWorkspacesConfig& options);
  void runActivate() const {
    if (onActivate_) onActivate_();
  }
  virtual void setLabel(const QString& text) = 0;
  virtual void updateIcons() {}
  virtual void repolishAll() = 0;

  const config::options::GlazewmWorkspacesConfig& options_;

 private:
  void updateStatus();
  void updateLabel();

  GlazewmWorkspaceStatus status_ = GlazewmWorkspaceStatus::Empty;
  std::function<void()> onActivate_;
};

// Port of GlazewmWorkspaceButton.
class GlazewmWorkspaceButton final : public QPushButton, public GlazewmWorkspaceButtonBase {
  Q_OBJECT
 public:
  GlazewmWorkspaceButton(const QString& name, const QString& displayName,
                         const config::options::GlazewmWorkspacesConfig& options, QWidget* parent);

  QWidget* widget() override { return this; }
  QString labelText() const override { return text(); }
  void activate() override { click(); }

 protected:
  void setLabel(const QString& text) override { setText(text); }
  void repolishAll() override;
};

// hwnd -> pixmap in insertion order (Python dict[int, QPixmap | None]); a null pixmap is None.
using GlazewmIcons = std::vector<std::pair<long long, QPixmap>>;

// Port of GlazewmWorkspaceButtonWithIcons: a QFrame with a "label" QLabel and one "icon icon-N"
// QLabel per app icon. A left press activates the workspace.
class GlazewmIconButton final : public QFrame, public GlazewmWorkspaceButtonBase {
  Q_OBJECT
 public:
  // Resolves one window's icon (WorkspaceWidget._get_app_icon): `uniquePids` collects the process
  // ids already shown when hide_duplicates is on, `screen` is the button's screen.
  using IconLookup = std::function<QPixmap(long long hwnd, std::set<std::uint32_t>* uniquePids,
                                           const QScreen* screen)>;

  GlazewmIconButton(const QString& name, const QString& displayName,
                    const config::options::GlazewmWorkspacesConfig& options, IconLookup lookup,
                    std::vector<glazewm::Window> windows, QWidget* parent);

  QWidget* widget() override { return this; }
  QString labelText() const override;
  void activate() override { runActivate(); }

  void setWindows(std::vector<glazewm::Window> windows) { windows_ = std::move(windows); }
  // update_icons(): reloads every icon of the workspace and rebuilds the icon labels.
  void refreshIcons() { updateIcons(); }
  const GlazewmIcons& icons() const { return icons_; }
  const std::vector<QLabel*>& iconLabels() const { return iconLabels_; }
  QLabel* textLabel() const { return text_; }

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void setLabel(const QString& text) override;
  void updateIcons() override;
  void repolishAll() override;

 private:
  IconLookup lookup_;
  std::vector<glazewm::Window> windows_;
  QHBoxLayout* layout_ = nullptr;
  QLabel* text_ = nullptr;
  GlazewmIcons icons_;
  std::vector<QLabel*> iconLabels_;
  std::set<std::uint32_t> uniquePids_;
};

// Port of glazewm.workspaces.GlazewmWorkspacesWidget. With app_icons enabled the buttons are
// GlazewmIconButtons showing the icons of the workspace's windows.
//
// Threading: GUI thread only. GlazeWM data arrives as glazewm::kStateEvent EventBus events
// published by GlazewmService; commands leave as glazewm::kCommandEvent events. Window icons load
// on the TaskbarIcons worker thread and arrive through its iconsChanged signal.
class GlazewmWorkspacesWidget final : public BaseWidget {
  Q_OBJECT
 public:
  // Which monitor (GlazeWM monitor "handle", an HMONITOR) the bar window hosting this widget is on.
  using MonitorResolver = std::function<std::optional<long long>()>;

  explicit GlazewmWorkspacesWidget(const WidgetContext& context);
  ~GlazewmWorkspacesWidget() override;

  void setMonitorResolver(MonitorResolver resolver) { monitorResolver_ = std::move(resolver); }
  // Replaces the window system the app icons come from (tests); `async` false loads inline.
  void setWindowSystem(std::shared_ptr<platform::WindowSystem> system, bool async = false);

  // The two GlazeWM notifications (also used directly by tests). onWorkspaces throws FormatError
  // on a bad label template, like the Python slot raises KeyError.
  void onConnection(bool connected);
  void onWorkspaces(const glazewm::MonitorsPtr& monitors);
  // Wheel handling with a QWheelEvent::angleDelta().y() value.
  void scrollWorkspaces(int deltaY);

  // The buttons in layout order (Python workspace_container_layout).
  std::vector<GlazewmWorkspaceButtonBase*> layoutButtons() const;
  GlazewmWorkspaceButtonBase* buttonByName(const QString& name) const;
  QLabel* offlineLabel() const { return offline_; }
  std::optional<long long> monitorHandle() const { return monitorHandle_; }

 protected:
  void showEvent(QShowEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  void onBusEvent(const QString& name, const QVariantMap& payload);
  std::optional<long long> resolveMonitor() const;
  bool appIconsEnabled() const;
  GlazewmWorkspaceButtonBase* createButton(const glazewm::Workspace& workspace);
  void refreshIcons();
  QPixmap appIcon(long long hwnd, std::set<std::uint32_t>* uniquePids, const QScreen* screen);

  EventBus& bus_;
  config::options::GlazewmWorkspacesConfig options_;
  MonitorResolver monitorResolver_;
  QLabel* offline_ = nullptr;
  std::vector<GlazewmWorkspaceButtonBase*> buttons_;  // creation order (Python dict order)
  std::optional<long long> monitorHandle_;
  bool subscribed_ = false;
  bool online_ = false;
  bool iconsEnabled_ = false;

  std::shared_ptr<platform::WindowSystem> windowSystem_;
  TaskbarIcons* icons_ = nullptr;
};

}  // namespace yasb::widgets

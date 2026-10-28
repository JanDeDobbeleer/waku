#pragma once

#include <QByteArray>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "core/bar/theme_class.h"
#include "core/config/options/control_center.h"
#include "platform/power.h"
#include "widgets/control_center/control_center_system.h"
#include "widgets/menu_support/elided_label.h"

namespace yasb::widgets {

// What every section needs from the widget that built it.
struct SectionContext {
  const ControlCenterSystem& system;
  bar::GlobalState& globalState;
  bool tooltip = false;
  std::function<void()> refreshPopup;  // ControlCenterWidget._refresh_popup_state
  std::function<void()> dismissPopup;  // PowerOperations.clear_widget: hides the popup
};

// Sets the `class` property and repolishes when it changed.
void setCssClass(QWidget* widget, const QString& cssClass);

// QMenu styled as Python's apply_qmenu_style: Fusion, blur and round corners on show, with the
// "context-menu" class plus " dark" in dark mode. Deleted when closed.
QMenu* createContextMenu(QWidget* parent, bool dark);

// A frame that emits clicked() on a left press and release inside it.
class ClickFrame : public QFrame {
  Q_OBJECT
 public:
  explicit ClickFrame(QWidget* parent = nullptr) : QFrame(parent) {}

 signals:
  void clicked();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  bool pressed_ = false;
};

// --- system controls ----------------------------------------------------------------------------

class SystemControlsSection final : public QFrame {
  Q_OBJECT
 public:
  SystemControlsSection(QWidget* parent, const config::options::SystemControlsConfig& options,
                        SectionContext context);

  void refreshState();
  QPushButton* profileButton() const { return profileButton_; }
  QPushButton* settingsButton() const { return settingsButton_; }
  QPushButton* powerButton() const { return powerButton_; }
  // The menus the three buttons open (null until first opened). Tests drive their actions.
  QMenu* settingsMenu() const { return settingsMenu_; }
  QMenu* profileMenu() const { return profileMenu_; }
  QMenu* powerMenu() const { return powerMenu_; }

  void showSettingsMenu();
  void showProfileMenu();
  void showPowerMenu();

 protected:
  bool event(QEvent* event) override;

 private:
  void updateProfileImage();
  QPixmap buildProfileImage(int size, qreal dpr) const;
  void showMenu(QPointer<QMenu>& slot, QPushButton* button, bool rightAlign, int gap,
                const std::function<void(QMenu*)>& fill);
  void runPowerAction(const std::function<void()>& action);

  config::options::SystemControlsConfig options_;
  SectionContext context_;
  QPushButton* profileButton_ = nullptr;
  QPushButton* settingsButton_ = nullptr;
  QPushButton* powerButton_ = nullptr;
  QPointer<QMenu> settingsMenu_;
  QPointer<QMenu> profileMenu_;
  QPointer<QMenu> powerMenu_;
  qreal profileDpr_ = 0;
};

// --- quick actions ------------------------------------------------------------------------------

class QuickActionsSection final : public QFrame {
  Q_OBJECT
 public:
  QuickActionsSection(QWidget* parent, const config::options::QuickActionsSectionConfig& options,
                      SectionContext context);

  void refreshState();
  ClickFrame* button(const QString& id) const;
  void runAction(const config::options::ControlCenterActionConfig& action);

 private:
  ClickFrame* createButton(const config::options::ControlCenterActionConfig& action);
  bool isActive(const std::string& id) const;
  bool isDisabled(const std::string& id) const;

  config::options::QuickActionsSectionConfig options_;
  SectionContext context_;
  std::map<std::string, ClickFrame*> buttons_;
  std::optional<QString> theme_;
  std::optional<QString> dnd_;
};

// --- sliders ------------------------------------------------------------------------------------

class SlidersSection final : public QFrame {
  Q_OBJECT
 public:
  SlidersSection(QWidget* parent, const config::options::SlidersSectionConfig& options,
                 SectionContext context, qulonglong widgetMonitor);

  void refreshState();
  // BrightnessService.brightness_changed while the popup is open.
  void updateBrightness(qulonglong monitor, int percent);

  QSlider* volumeSlider() const { return volumeSlider_; }
  QSlider* microphoneSlider() const { return microphoneSlider_; }
  QSlider* brightnessSlider() const { return brightnessSlider_; }
  QLabel* volumeValue() const { return volumeValue_; }
  QLabel* microphoneValue() const { return microphoneValue_; }
  QLabel* brightnessValue() const { return brightnessValueLabel_; }
  QPushButton* volumeSource() const { return volumeSourceButton_; }
  QPushButton* microphoneSource() const { return microphoneSourceButton_; }
  QPushButton* brightnessSource() const { return brightnessSourceButton_; }
  // The open source menu for key (volume, microphone, brightness), or null.
  QMenu* sourceMenu(const QString& key) const;

  void showVolumeSourceMenu();
  void showMicrophoneSourceMenu();
  void showBrightnessSourceMenu();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Row {
    QFrame* frame = nullptr;
    QSlider* slider = nullptr;
    QLabel* value = nullptr;
    QPushButton* source = nullptr;
  };
  Row createRow(const QString& key, const config::options::ControlCenterSliderConfig& options,
                const std::function<void(int)>& onChanged);
  std::pair<int, QString> initialState(const QString& key);
  std::optional<qulonglong> brightnessTarget() const;
  std::optional<int> currentBrightness() const;
  void onVolumeChanged(int value);
  void onMicrophoneChanged(int value);
  void onBrightnessChanged(int value);
  void onAudioChanged(platform::AudioEndpoints* endpoint, QLabel* label, int value);
  void showAudioSourceMenu(const QString& key, platform::AudioEndpoints* endpoint,
                           QPushButton* button);
  void refreshAudioRow(platform::AudioEndpoints* endpoint, const QString& key, QSlider* slider,
                       QLabel* label);
  void popupMenu(QMenu* menu, QPushButton* button);

  config::options::SlidersSectionConfig options_;
  SectionContext context_;
  qulonglong widgetMonitor_;
  std::optional<qulonglong> selectedMonitor_;
  QSlider* volumeSlider_ = nullptr;
  QLabel* volumeValue_ = nullptr;
  QPushButton* volumeSourceButton_ = nullptr;
  QSlider* microphoneSlider_ = nullptr;
  QLabel* microphoneValue_ = nullptr;
  QPushButton* microphoneSourceButton_ = nullptr;
  QSlider* brightnessSlider_ = nullptr;
  QLabel* brightnessValueLabel_ = nullptr;
  QPushButton* brightnessSourceButton_ = nullptr;
  std::map<QString, QPointer<QMenu>> menus_;
};

// --- power --------------------------------------------------------------------------------------

class PowerSection final : public QFrame {
  Q_OBJECT
 public:
  PowerSection(QWidget* parent, const config::options::PowerSectionConfig& options,
               SectionContext context);
  ~PowerSection() override;

  void refreshState();
  ClickFrame* planButton() const { return planButton_; }
  ClickFrame* modeButton() const { return modeButton_; }
  ElidedLabel* planLabel() const { return planLabel_; }
  ElidedLabel* modeLabel() const { return modeLabel_; }
  bool modeEnabled() const { return modeEnabled_; }
  QMenu* menu(const QString& key) const;

  void showPlanMenu();
  void showModeMenu();

 private:
  void setupButtonLayout(QWidget* button, const QString& title, ElidedLabel* label,
                         const QString& icon);
  void showMenu(const QString& key, ClickFrame* button,
                const std::vector<std::pair<QString, QUuid>>& items,
                const std::function<void(const QUuid&, const QString&)>& onSelect);
  void closeMenus();
  void updatePlansState();
  void applyPlan(const QUuid& guid, const QString& name);
  void applyMode(const QUuid& guid, const QString& name);
  void buildModeButton(QGridLayout* grid);
  void applyModeButtonState();
  void refreshModeState();
  static QString modeName(const std::optional<platform::PowerMode>& mode);

  config::options::PowerSectionConfig options_;
  SectionContext context_;
  std::vector<platform::PowerPlan> plans_;
  std::optional<QUuid> activePlan_;
  std::map<QString, QPointer<QMenu>> menus_;
  ClickFrame* planButton_ = nullptr;
  ElidedLabel* planLabel_ = nullptr;
  ClickFrame* modeButton_ = nullptr;
  ElidedLabel* modeLabel_ = nullptr;
  bool modeEnabled_ = false;
};

// --- media --------------------------------------------------------------------------------------

class MediaSection final : public QFrame {
  Q_OBJECT
 public:
  MediaSection(QWidget* parent, const config::options::MediaSectionConfig& options,
               SectionContext context);

  void refreshState();
  QLabel* thumbnail() const { return thumbnail_; }
  QLabel* title() const { return title_; }
  QLabel* artist() const { return artist_; }
  QLabel* prevButton() const { return prev_; }
  QLabel* playButton() const { return play_; }
  QLabel* nextButton() const { return next_; }
  QTimer* hideTimer() { return &hideTimer_; }
  // Hides the section when the session list stayed empty (hide timer expiry).
  void hideNow();

 protected:
  bool event(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  void syncUi(bool forceThumbnail);
  void updateControls();
  void updateControlsFrom(const platform::MediaSessionState& session);
  void applyThumbnail(const platform::MediaSessionState* session);
  QPixmap renderThumbnail(const QImage& source) const;
  QPixmap roundedThumbnail(const QImage& square) const;
  void updateThumbnailScale();

  config::options::MediaSectionConfig options_;
  SectionContext context_;
  QLabel* thumbnail_ = nullptr;
  ElidedLabel* title_ = nullptr;
  ElidedLabel* artist_ = nullptr;
  QLabel* prev_ = nullptr;
  QLabel* play_ = nullptr;
  QLabel* next_ = nullptr;
  QTimer hideTimer_;
  QPixmap emptyThumbnail_;
  QByteArray thumbnailBytes_;
  QString currentAppId_;
  QString currentTitle_;
  QString currentArtist_;
  std::optional<bool> currentPlaying_;
  qreal thumbDpr_ = 1.0;
};

}  // namespace yasb::widgets

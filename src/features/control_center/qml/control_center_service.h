#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <QVariantList>
#include <functional>
#include <optional>
#include <vector>

#include "features/brightness/brightness.h"
#include "features/control_center/power.h"

namespace wabar::control_center {

// The system side of the control center as a QML singleton (ControlCenter): Do Not Disturb, the
// app theme, the touch keyboard, the screenshot overlay, monitor brightness, power plans and modes,
// the user's name and picture, and the session and power actions. Volume and microphone live in
// AudioOutput and AudioInput, the player in Media; this object does not repeat them. GUI thread
// only. The brightness object lives on this object (QObject child) and owns its own worker
// threads (see wabar::brightness::BrightnessControl); the DND watcher is the platform's WNF
// subscription. The engine destroys the singleton, which stops both.
//
// Every state change a widget can see is announced with `changed`; `refresh()` re-reads everything
// and is meant to run when the popup opens.
//
// Dangerous actions go through requestAction(): "lock", "sleep" and "hibernate" run at once;
// "signOut", "restart" and "shutdown" need a second call with the same id within kConfirmMs (the
// first call only sets `pendingAction`). cancelAction() clears the pending one (popup closed).
// Nothing runs the action itself except the Launcher, which tests replace.
class ControlCenter : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(QString dnd READ dnd NOTIFY changed FINAL)
  Q_PROPERTY(bool dndActive READ dndActive NOTIFY changed FINAL)
  Q_PROPERTY(bool darkMode READ darkMode NOTIFY changed FINAL)
  Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY changed FINAL)
  Q_PROPERTY(int brightness READ brightness NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList brightnessSources READ brightnessSources NOTIFY changed FINAL)
  Q_PROPERTY(QString brightnessSource READ brightnessSource NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList powerPlans READ powerPlans NOTIFY changed FINAL)
  Q_PROPERTY(QString powerPlanName READ powerPlanName NOTIFY changed FINAL)
  Q_PROPERTY(bool powerModeAvailable READ powerModeAvailable NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList powerModes READ powerModes NOTIFY changed FINAL)
  Q_PROPERTY(QString powerModeName READ powerModeName NOTIFY changed FINAL)
  Q_PROPERTY(QString userName READ userName NOTIFY changed FINAL)
  Q_PROPERTY(QString avatarUrl READ avatarUrl NOTIFY changed FINAL)
  Q_PROPERTY(QString pendingAction READ pendingAction NOTIFY changed FINAL)

 public:
  static constexpr int kConfirmMs = 4000;

  // Reads and writes of the machine's settings. A missing function disables its control.
  struct Backend {
    std::function<QString()> dndStatus;  // "disabled", "priority", "alarms", "unknown"
    std::function<bool(const QString&)> setDndStatus;
    std::function<QString()> themeMode;                   // "light" or "dark"
    std::function<std::optional<QString>()> toggleTheme;  // the new mode
    std::function<void()> toggleTouchKeyboard;
    std::function<std::vector<PowerPlan>(std::optional<QUuid>*)> powerPlans;
    std::function<unsigned long(const QUuid&)> setPowerPlan;  // 0 on success
    std::function<bool()> powerModeSupported;
    std::function<std::vector<PowerMode>()> powerModes;
    std::function<std::optional<PowerMode>()> activePowerMode;
    std::function<bool(const QUuid&)> setPowerMode;
    std::function<QString()> userName;
    std::function<QString()> avatarPath;
  };

  // What leaves the process or ends the session. Replaced in tests so nothing runs.
  struct Launcher {
    std::function<void()> lock;
    std::function<void()> sleep;
    std::function<void()> hibernate;
    std::function<void()> signOut;
    std::function<void()> restart;
    std::function<void()> shutdown;
    // ShellExecute "open" of a URI, such as ms-settings:network or ms-screenclip:.
    std::function<void(const QString&)> open;
  };

  // Real system. On the offscreen platform every control is inert and nothing touches the machine.
  explicit ControlCenter(QObject* parent = nullptr);
  // Test seam. `brightness` may be null (no brightness control); it is reparented to this object.
  ControlCenter(Backend backend, Launcher launcher,
                wabar::brightness::BrightnessControl* brightness, QObject* parent);
  ~ControlCenter() override;

  bool available() const { return available_; }
  QString dnd() const { return dnd_; }
  // Anything but "disabled" is on, "unknown" included (Python YASB does the same); empty means
  // there is no DND control at all.
  bool dndActive() const { return !dnd_.isEmpty() && dnd_ != QLatin1String("disabled"); }
  bool darkMode() const { return theme_ == QLatin1String("dark"); }
  bool brightnessAvailable() const { return brightnessMonitor_.has_value(); }
  int brightness() const { return brightness_; }
  QVariantList brightnessSources() const;
  QString brightnessSource() const;
  QVariantList powerPlans() const;
  QString powerPlanName() const;
  bool powerModeAvailable() const { return modeSupported_; }
  QVariantList powerModes() const;
  QString powerModeName() const { return modeName_; }
  QString userName() const { return userName_; }
  QString avatarUrl() const;
  QString pendingAction() const { return pending_; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void toggleDnd();
  Q_INVOKABLE void cycleDnd();
  Q_INVOKABLE void toggleTheme();
  Q_INVOKABLE void toggleTouchKeyboard();
  Q_INVOKABLE void screenshot();
  Q_INVOKABLE void openSettings(const QString& uri);
  Q_INVOKABLE void setBrightness(int percent);
  Q_INVOKABLE void setBrightnessSource(const QString& id);
  Q_INVOKABLE bool setPowerPlan(const QString& id);
  Q_INVOKABLE bool setPowerMode(const QString& id);
  // True when the action ran now, false when it is now waiting for its confirming call, was
  // unknown, or is not available.
  Q_INVOKABLE bool requestAction(const QString& id);
  Q_INVOKABLE void cancelAction();

  // The platform's DND event (the real object connects the WNF watcher to this); public so tests
  // can deliver one.
  void onDndStatus(const QString& status);

 signals:
  void changed();

 private:
  void init();
  void readBrightness();
  void readPower();
  void clearPending();

  Backend backend_;
  Launcher launcher_;
  wabar::brightness::BrightnessControl* brightnessControl_ = nullptr;
  QTimer pendingTimer_;
  bool available_ = true;

  QString dnd_;
  QString theme_;
  std::optional<qulonglong> brightnessMonitor_;
  std::optional<qulonglong> selectedMonitor_;
  int brightness_ = 0;
  std::vector<PowerPlan> plans_;
  std::optional<QUuid> activePlan_;
  bool modeSupported_ = false;
  std::vector<PowerMode> modes_;
  QString modeName_;
  QString userName_;
  QString avatarPath_;
  QString pending_;
};

}  // namespace wabar::control_center

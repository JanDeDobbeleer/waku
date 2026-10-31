#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace wabar::notifications {

class WnfSubscription;

// Windows Focus Assist / Do Not Disturb (services/dnd/dnd_api.py DndService), through the
// undocumented IQuietHoursSettings COM object. Modes: "disabled", "priority", "alarms".

// "unknown" when the settings object cannot be created or read.
QString dndStatus();
// False for an unknown mode or when the write fails.
bool setDndStatus(const QString& mode);

// DndService.initialize_wnf_listener: emits statusChanged(status) on the GUI thread whenever the
// Focus Assist profile changes (WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED). The WNF callback runs
// on a system thread: it reads the status there (a short out-of-process COM call, as Python does)
// and posts the result. stop() (or the destructor) unsubscribes and returns only after a running
// callback has left, so it can wait for one COM call (WnfSubscription, win32/wnf_win.h).
class DndWatcher final : public QObject {
  Q_OBJECT

 public:
  static constexpr const char* kServiceKey = "dnd-watcher";

  explicit DndWatcher(QObject* parent = nullptr);
  ~DndWatcher() override;

  // False when WNF is missing or the subscription fails (the status then never changes by event).
  bool start();
  Q_INVOKABLE void stop();

  // Delivers a status as the WNF callback would (tests).
  void deliver(const QString& status);

 signals:
  void statusChanged(const QString& status);

 private:
  std::unique_ptr<WnfSubscription> wnf_;  // null when stopped
};

}  // namespace wabar::notifications

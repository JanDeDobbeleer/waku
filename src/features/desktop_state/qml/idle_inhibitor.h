#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>

#include "features/desktop_state/desktop_probe.h"

namespace waku::desktop_state {

// Keeps the display and the system awake while `enabled` is true (Quickshell.Wayland's
// IdleInhibitor). Windows holds the state with SetThreadExecutionState(ES_CONTINUOUS |
// ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) on the GUI thread; it is released when `enabled`
// turns false and when the object is destroyed. Several inhibitors share one execution state: the
// machine sleeps again when the last one lets go. `window` is accepted for compatibility and
// ignored, Windows inhibits for the whole session. GUI thread only.
class IdleInhibitor : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
  Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged FINAL)

 public:
  explicit IdleInhibitor(QObject* parent = nullptr);
  // Test seam: a counter over a fake probe.
  IdleInhibitor(IdleInhibitCounter* counter, QObject* parent);
  ~IdleInhibitor() override;

  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled);
  QObject* window() const { return window_.data(); }
  void setWindow(QObject* window);

 signals:
  void enabledChanged();
  void windowChanged();

 private:
  IdleInhibitCounter* counter_;
  bool enabled_ = false;
  QPointer<QObject> window_;
};

}  // namespace waku::desktop_state

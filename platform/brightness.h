#pragma once

#include <QObject>
#include <QString>
#include <optional>
#include <vector>

namespace yasb::platform {

struct BrightnessMonitor {
  qulonglong id = 0;  // HMONITOR
  QString name;
};

// Native handle of the monitor that holds `hwnd` (MonitorFromWindow, nearest); 0 if unknown.
qulonglong monitorForWindow(quintptr hwnd);

// Monitor brightness (Python BrightnessService): DDC/CI for external panels, the active power
// scheme's brightness for the internal panel. Only controllable monitors are listed.
//
// Threading: the object lives on the GUI thread. The system implementation runs detection, DDC
// reads and slider writes on one worker thread (slider drags coalesce to the newest value per
// monitor) and re-posts results to the GUI thread. stop() joins the worker.
class BrightnessControl : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  virtual std::vector<BrightnessMonitor> monitors() = 0;
  // "Monitor 2 · Hdmi", or "Monitor 2" without a connection type.
  virtual QString monitorSubtitle(qulonglong id, int index) = 0;
  virtual std::optional<int> brightness(qulonglong id) = 0;
  virtual void setBrightness(qulonglong id, int percent) = 0;
  // Re-reads DDC monitors in the background; the scheme panel is re-read at once.
  virtual void refreshNow() = 0;

  Q_INVOKABLE virtual void stop() {}

 signals:
  void brightnessChanged(qulonglong id, int percent);
};

// DDC/CI + power-scheme implementation. Never null.
BrightnessControl* createSystemBrightness(QObject* parent = nullptr);

}  // namespace yasb::platform

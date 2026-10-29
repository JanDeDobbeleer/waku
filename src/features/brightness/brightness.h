#pragma once

#include <QObject>
#include <QString>
#include <optional>
#include <vector>

namespace wabar::brightness {

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
// monitor) and re-posts results to the GUI thread. A second thread owns a message-only window that
// watches the video-brightness power setting. stop() joins both.
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

  // DDC contrast (VCP 0x12). Only DDC monitors that answer the read support it; the default
  // implementation reports none (BrightnessService.get_contrast / set_contrast /
  // supports_contrast).
  virtual bool supportsContrast(qulonglong) { return false; }
  virtual std::optional<int> contrast(qulonglong) { return std::nullopt; }
  virtual void setContrast(qulonglong, int) {}

  Q_INVOKABLE virtual void stop() {}

 signals:
  void brightnessChanged(qulonglong id, int percent);
  void contrastChanged(qulonglong id, int percent);
};

// DDC/CI + power-scheme implementation. Never null. `ddcPollSeconds` is the background DDC re-read
// period (0 disables it); the control center uses the Python default of 60.
BrightnessControl* createSystemBrightness(QObject* parent = nullptr, int ddcPollSeconds = 60);

}  // namespace wabar::brightness

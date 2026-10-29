#pragma once

#include <QObject>
#include <QString>
#include <optional>
#include <vector>

namespace wabar::audio {

enum class AudioFlow { Render, Capture };

struct AudioDevice {
  QString id;
  QString name;
};

// Default audio endpoint of one direction (Python AudioOutputService / AudioInputService): master
// volume and mute of the default device, the active device list, and default-device switching.
//
// Threading: GUI thread only. The system implementation registers Core Audio callbacks that fire
// on COM threads; they only re-post to the GUI thread, which re-resolves the endpoint and emits.
// stop() releases the callbacks (the runtime's service registry calls it before shutdown).
class AudioEndpoints : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  // True when a default endpoint with a volume interface exists.
  virtual bool available() = 0;
  // round(GetMasterVolumeLevelScalar() * 100); nullopt without an endpoint.
  virtual std::optional<int> volumePercent() = 0;
  virtual bool setVolumePercent(int percent) = 0;
  virtual std::optional<bool> muted() = 0;
  virtual bool setMuted(bool muted) = 0;
  virtual std::vector<AudioDevice> devices() = 0;
  virtual QString defaultDeviceId() = 0;
  virtual bool setDefaultDevice(const QString& id) = 0;

  Q_INVOKABLE virtual void stop() {}

 signals:
  // Volume or mute of the default endpoint changed.
  void volumeChanged();
  // The default endpoint or the set of active devices changed.
  void deviceChanged();
};

// Core Audio implementation. Never null; without a usable endpoint every query reports "none".
AudioEndpoints* createSystemAudio(AudioFlow flow, QObject* parent = nullptr);

}  // namespace wabar::audio

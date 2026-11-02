#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QVariantList>

#include "features/audio/audio_endpoints.h"

namespace waku::audio {

// Default audio endpoint of one direction as a QML singleton (AudioOutput, AudioInput). Created
// by the engine on first use and shared by every widget. GUI thread only: AudioEndpoints
// re-posts its Core Audio callbacks to the GUI thread, and `changed` is emitted from there.
// The endpoint is stopped (callbacks released) when the engine destroys the singleton. Off the
// live desktop (kernel::liveDesktop("audio"): tests, the offscreen platform) there is no endpoint:
// nothing is available, reads are zero or empty and writes do nothing.
// The Core Audio endpoint on a live desktop (kernel::liveDesktop("audio")), else null.
AudioEndpoints* systemAudioIfLive(AudioFlow flow);

class AudioBase : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
  Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY changed FINAL)
  Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY changed FINAL)
  Q_PROPERTY(QString defaultDevice READ defaultDevice NOTIFY changed FINAL)

 public:
  ~AudioBase() override;

  bool available() const;
  int volume() const;
  void setVolume(int percent);
  bool muted() const;
  void setMuted(bool muted);
  QVariantList devices() const;
  QString defaultDevice() const;

  Q_INVOKABLE bool setDefaultDevice(const QString& id);

 signals:
  void changed();

 protected:
  // Takes ownership of `endpoints` (also the test seam: pass a fake); null is the inert service.
  AudioBase(AudioEndpoints* endpoints, QObject* parent);

 private:
  AudioEndpoints* endpoints_;
};

class AudioOutput : public AudioBase {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit AudioOutput(QObject* parent = nullptr)
      : AudioBase(systemAudioIfLive(AudioFlow::Render), parent) {}
};

class AudioInput : public AudioBase {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit AudioInput(QObject* parent = nullptr)
      : AudioBase(systemAudioIfLive(AudioFlow::Capture), parent) {}
};

}  // namespace waku::audio

#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <vector>

namespace wabar::audio {

// One audio session of the default output device (an app's stream in the Windows volume mixer).
struct AudioStreamInfo {
  QString id;    // the session instance identifier; unique while the session lives
  QString name;  // display name of the session, else the process's executable name
  QString icon;  // icon file of the session, else the executable path ("" when unknown)
  std::uint32_t pid = 0;
  double volume = 1.0;  // 0..1, the session's own volume (ISimpleAudioVolume)
  bool muted = false;
  bool active = false;  // the session is rendering sound right now
};

// The per-app audio sessions of the default render endpoint (IAudioSessionManager2).
//
// Threading: GUI thread only. The system implementation registers Core Audio callbacks that fire
// on COM threads; they only re-post to the GUI thread, which emits `streamsChanged`. stop()
// releases the callbacks.
class AudioStreamSource : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  // The current sessions, system sounds excluded and expired sessions dropped.
  virtual std::vector<AudioStreamInfo> streams() = 0;
  virtual bool setVolume(const QString& id, double volume) = 0;
  virtual bool setMuted(const QString& id, bool muted) = 0;

  Q_INVOKABLE virtual void stop() {}

 signals:
  // A session appeared, ended, started or stopped rendering, or changed volume or mute.
  void streamsChanged();
};

// Core Audio implementation. Never null; without an endpoint `streams()` is empty.
AudioStreamSource* createSystemAudioStreams(QObject* parent = nullptr);

}  // namespace wabar::audio

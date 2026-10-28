#pragma once

#include <QString>
#include <memory>

namespace yasb::platform {

// One bound per-application audio session (pycaw SimpleAudioVolume of the session that belongs to
// the current media app). GUI thread only. Every call returns false / nullopt on failure.
class AppVolumeSession {
 public:
  virtual ~AppVolumeSession() = default;
  // Master volume in 0.0 .. 1.0.
  virtual bool volume(float* out) = 0;
  virtual bool setVolume(float level) = 0;
  virtual bool muted(bool* out) = 0;
  virtual bool setMuted(bool muted) = 0;
};

// Finds the audio session of a media app (media._bind_app_volume_session): match by the process
// AUMID first, then by executable name. Abstract so tests never touch live audio.
class AppVolumeSource {
 public:
  virtual ~AppVolumeSource() = default;
  // Null when no session matches.
  virtual std::unique_ptr<AppVolumeSession> bind(const QString& aumid) = 0;
};

// Core Audio implementation over the default render device's session manager. Never null.
std::shared_ptr<AppVolumeSource> createSystemAppVolumeSource();

}  // namespace yasb::platform

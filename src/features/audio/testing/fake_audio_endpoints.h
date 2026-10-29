#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "features/audio/audio_endpoints.h"

namespace wabar::audio {

// Test double of AudioEndpoints: one default endpoint with two devices, no Core Audio.
class FakeEndpoints final : public AudioEndpoints {
 public:
  bool available() override { return hasEndpoint; }
  std::optional<int> volumePercent() override {
    return hasEndpoint ? std::optional<int>(volume) : std::nullopt;
  }
  bool setVolumePercent(int percent) override {
    if (!hasEndpoint) return false;
    volume = percent;
    return true;
  }
  std::optional<bool> muted() override {
    return hasEndpoint ? std::optional<bool>(isMuted) : std::nullopt;
  }
  bool setMuted(bool value) override {
    if (!hasEndpoint) return false;
    isMuted = value;
    return true;
  }
  std::vector<AudioDevice> devices() override {
    return {{QStringLiteral("id-1"), QStringLiteral("Speakers")},
            {QStringLiteral("id-2"), QStringLiteral("Headphones")}};
  }
  QString defaultDeviceId() override { return defaultId; }
  bool setDefaultDevice(const QString& id) override {
    defaultId = id;
    return true;
  }
  void stop() override {
    if (stopCounter) ++*stopCounter;
  }

  bool hasEndpoint = true;
  int volume = 30;
  bool isMuted = false;
  QString defaultId = QStringLiteral("id-1");
  int* stopCounter = nullptr;
};

}  // namespace wabar::audio

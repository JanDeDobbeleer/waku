#pragma once

#include <QString>
#include <vector>

#include "features/audio/audio_stream_source.h"

namespace waku::audio {

// Test double of AudioStreamSource: a list tests edit, no Core Audio.
class FakeStreams final : public AudioStreamSource {
 public:
  std::vector<AudioStreamInfo> streams() override { return list; }
  bool setVolume(const QString& id, double volume) override {
    for (AudioStreamInfo& info : list) {
      if (info.id == id) {
        info.volume = volume;
        return true;
      }
    }
    return false;
  }
  bool setMuted(const QString& id, bool muted) override {
    for (AudioStreamInfo& info : list) {
      if (info.id == id) {
        info.muted = muted;
        return true;
      }
    }
    return false;
  }
  void stop() override {
    if (stopCounter) ++*stopCounter;
  }

  // Replaces the list and reports the change, as Core Audio's callbacks would.
  void publish(std::vector<AudioStreamInfo> infos) {
    list = std::move(infos);
    emit streamsChanged();
  }

  std::vector<AudioStreamInfo> list;
  int* stopCounter = nullptr;
};

}  // namespace waku::audio

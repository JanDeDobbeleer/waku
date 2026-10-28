#include <gtest/gtest.h>

#include <QSignalSpy>
#include <memory>

#include "spike/services/audio_service.h"

using namespace barnacle::services;
namespace platform = yasb::platform;

namespace {

class FakeEndpoints final : public platform::AudioEndpoints {
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
  std::vector<platform::AudioDevice> devices() override {
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

// Exposes the protected constructor; keeps a pointer to the fake the base class now owns.
class TestAudio final : public AudioBase {
 public:
  explicit TestAudio(FakeEndpoints* fake) : AudioBase(fake, nullptr), fake_(fake) {}
  FakeEndpoints* fake() const { return fake_; }

 private:
  FakeEndpoints* fake_;
};

}  // namespace

TEST(AudioService, ReadsTheEndpoint) {
  TestAudio audio(new FakeEndpoints);
  EXPECT_TRUE(audio.available());
  EXPECT_EQ(audio.volume(), 30);
  EXPECT_FALSE(audio.muted());
  EXPECT_EQ(audio.defaultDevice(), QStringLiteral("id-1"));
}

TEST(AudioService, SetVolumeClampsToZeroAndHundredAndNotifies) {
  TestAudio audio(new FakeEndpoints);
  QSignalSpy changed(&audio, &AudioBase::changed);
  audio.setVolume(150);
  EXPECT_EQ(audio.fake()->volume, 100);
  audio.setVolume(-5);
  EXPECT_EQ(audio.fake()->volume, 0);
  EXPECT_EQ(changed.count(), 2);
}

TEST(AudioService, SetMutedWritesThrough) {
  TestAudio audio(new FakeEndpoints);
  QSignalSpy changed(&audio, &AudioBase::changed);
  audio.setMuted(true);
  EXPECT_TRUE(audio.fake()->isMuted);
  EXPECT_TRUE(audio.muted());
  EXPECT_EQ(changed.count(), 1);
}

TEST(AudioService, ListsDevicesWithIdAndName) {
  TestAudio audio(new FakeEndpoints);
  const QVariantList devices = audio.devices();
  ASSERT_EQ(devices.size(), 2);
  EXPECT_EQ(devices[0].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("id-1"));
  EXPECT_EQ(devices[1].toMap().value(QStringLiteral("name")).toString(),
            QStringLiteral("Headphones"));
}

TEST(AudioService, SetDefaultDeviceSwitchesTheEndpoint) {
  TestAudio audio(new FakeEndpoints);
  EXPECT_TRUE(audio.setDefaultDevice(QStringLiteral("id-2")));
  EXPECT_EQ(audio.defaultDevice(), QStringLiteral("id-2"));
}

TEST(AudioService, EndpointSignalsBecomeChanged) {
  TestAudio audio(new FakeEndpoints);
  QSignalSpy changed(&audio, &AudioBase::changed);
  emit audio.fake()->volumeChanged();
  emit audio.fake()->deviceChanged();
  EXPECT_EQ(changed.count(), 2);
}

TEST(AudioService, WithoutAnEndpointNothingIsAvailableAndWritesAreIgnored) {
  auto* fake = new FakeEndpoints;
  fake->hasEndpoint = false;
  TestAudio audio(fake);
  QSignalSpy changed(&audio, &AudioBase::changed);
  EXPECT_FALSE(audio.available());
  EXPECT_EQ(audio.volume(), 0);
  EXPECT_FALSE(audio.muted());
  audio.setVolume(50);
  audio.setMuted(true);
  EXPECT_EQ(changed.count(), 0);
}

TEST(AudioService, DestructionStopsTheEndpointCallbacksOnce) {
  int stops = 0;
  auto* fake = new FakeEndpoints;
  fake->stopCounter = &stops;
  auto audio = std::make_unique<TestAudio>(fake);
  EXPECT_EQ(stops, 0);
  audio.reset();  // the base class stops the endpoint, then deletes it as a child
  EXPECT_EQ(stops, 1);
}

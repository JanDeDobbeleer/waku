#include <gtest/gtest.h>

#include <QSignalSpy>
#include <memory>

#include "features/audio/qml/audio_service.h"
#include "features/audio/testing/fake_audio_endpoints.h"

using namespace wabar::audio;

namespace {

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

// The QML singletons off the live desktop (the offscreen platform of the tests) never reach Core
// Audio: no endpoint, nothing available, writes ignored.
TEST(AudioService, TheSingletonsAreInertOffTheLiveDesktop) {
  for (AudioBase* audio :
       {static_cast<AudioBase*>(new AudioOutput), static_cast<AudioBase*>(new AudioInput)}) {
    QSignalSpy changed(audio, &AudioBase::changed);
    EXPECT_FALSE(audio->available());
    EXPECT_EQ(audio->volume(), 0);
    EXPECT_FALSE(audio->muted());
    EXPECT_TRUE(audio->devices().isEmpty());
    EXPECT_TRUE(audio->defaultDevice().isEmpty());
    audio->setVolume(50);
    audio->setMuted(true);
    EXPECT_FALSE(audio->setDefaultDevice(QStringLiteral("id-1")));
    EXPECT_EQ(changed.count(), 0);
    delete audio;
  }
}

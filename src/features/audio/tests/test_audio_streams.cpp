#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <memory>

#include "features/audio/qml/audio_streams.h"
#include "features/audio/testing/fake_audio_streams.h"

using namespace wabar::audio;

namespace {

AudioStreamInfo stream(const QString& id, const QString& name, double volume = 0.5) {
  AudioStreamInfo info;
  info.id = id;
  info.name = name;
  info.icon = QStringLiteral("C:/apps/") + name + QStringLiteral(".exe");
  info.pid = 100;
  info.volume = volume;
  info.active = true;
  return info;
}

struct Harness {
  Harness() {
    auto* fake = new FakeStreams;
    source = fake;
    streams = std::make_unique<AudioStreams>(fake, nullptr);
  }
  FakeStreams* source = nullptr;
  std::unique_ptr<AudioStreams> streams;
};

}  // namespace

TEST(AudioStreams, IsEmptyAtFirst) {
  Harness h;
  EXPECT_TRUE(h.streams->available());
  EXPECT_EQ(h.streams->streams()->count(), 0);
}

TEST(AudioStreams, WithoutASourceIsUnavailableAndEmpty) {
  AudioStreams none(nullptr, nullptr);
  EXPECT_FALSE(none.available());
  EXPECT_EQ(none.streams()->count(), 0);
}

TEST(AudioStreams, ListsEverySessionWithItsFields) {
  Harness h;
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify"), 0.25),
                     stream(QStringLiteral("b"), QStringLiteral("Firefox"))});
  ASSERT_EQ(h.streams->streams()->count(), 2);
  AudioStream* a = h.streams->streams()->values().at(0);
  EXPECT_EQ(a->name(), QStringLiteral("Spotify"));
  EXPECT_EQ(a->icon(), QStringLiteral("C:/apps/Spotify.exe"));
  EXPECT_EQ(a->pid(), 100);
  EXPECT_DOUBLE_EQ(a->volume(), 0.25);
  EXPECT_FALSE(a->muted());
  EXPECT_TRUE(a->active());
  EXPECT_TRUE(a->isStream());
  EXPECT_EQ(a->audio(), a);
  EXPECT_EQ(a->properties().value(QStringLiteral("application.name")).toString(),
            QStringLiteral("Spotify"));
}

TEST(AudioStreams, KeepsTheObjectWhileTheSessionLivesAndEmitsChanged) {
  Harness h;
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify"), 0.25)});
  AudioStream* a = h.streams->streams()->values().at(0);
  QSignalSpy changed(a, &AudioStream::changed);
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify"), 0.75)});
  EXPECT_EQ(h.streams->streams()->values().at(0), a);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_DOUBLE_EQ(a->volume(), 0.75);
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify"), 0.75)});
  EXPECT_EQ(changed.count(), 1);
}

TEST(AudioStreams, EndedSessionsLeaveTheList) {
  Harness h;
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify")),
                     stream(QStringLiteral("b"), QStringLiteral("Firefox"))});
  h.source->publish({stream(QStringLiteral("b"), QStringLiteral("Firefox"))});
  ASSERT_EQ(h.streams->streams()->count(), 1);
  EXPECT_EQ(h.streams->streams()->values().at(0)->name(), QStringLiteral("Firefox"));
}

TEST(AudioStreams, VolumeAndMuteWriteThrough) {
  Harness h;
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify"))});
  AudioStream* a = h.streams->streams()->values().at(0);
  QSignalSpy changed(a, &AudioStream::changed);
  a->setVolume(0.9);
  EXPECT_DOUBLE_EQ(h.source->list[0].volume, 0.9);
  EXPECT_DOUBLE_EQ(a->volume(), 0.9);
  a->setVolume(3.0);
  EXPECT_DOUBLE_EQ(h.source->list[0].volume, 1.0);
  a->setVolume(-1.0);
  EXPECT_DOUBLE_EQ(h.source->list[0].volume, 0.0);
  a->setMuted(true);
  EXPECT_TRUE(h.source->list[0].muted);
  EXPECT_TRUE(a->muted());
  EXPECT_EQ(changed.count(), 4);
}

TEST(AudioStreams, DestructionStopsTheSource) {
  int stops = 0;
  {
    auto* fake = new FakeStreams;
    fake->stopCounter = &stops;
    AudioStreams streams(fake, nullptr);
  }
  EXPECT_EQ(stops, 1);
}

TEST(AudioStreams, IsUsableFromQmlLikeAPipewireNode) {
  Harness h;
  h.source->publish({stream(QStringLiteral("a"), QStringLiteral("Spotify"), 0.25),
                     stream(QStringLiteral("b"), QStringLiteral("Firefox"), 0.5)});
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("streams"), h.streams.get());
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import wabar.services
QtObject {
    readonly property list<AudioStream> list: streams.streams.values
    property int count: list.length
    property real firstVolume: list[0].audio.volume
    property string firstName: list[0].properties["application.name"]
    Component.onCompleted: { list[1].audio.muted = true; list[0].audio.volume = 0.6; }
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_EQ(root->property("count").toInt(), 2);
  EXPECT_DOUBLE_EQ(root->property("firstVolume").toDouble(), 0.6);  // follows the write
  EXPECT_EQ(root->property("firstName").toString(), QStringLiteral("Spotify"));
  EXPECT_TRUE(h.source->list[1].muted);
  EXPECT_DOUBLE_EQ(h.source->list[0].volume, 0.6);
}

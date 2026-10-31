#include <gtest/gtest.h>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <memory>
#include <vector>

#include "features/media/qml/media_service.h"
#include "features/media/qml/mpris.h"
#include "features/media/testing/fake_media_source.h"

using namespace wabar::media;
using namespace wabar::media::testing;

namespace {

MediaSessionData player(const QString& appId, const QString& title, bool playing = true) {
  MediaSessionData data;
  data.appId = appId;
  data.sourceName = appId + QStringLiteral(" name");
  MediaProperties properties;
  properties.title = title;
  properties.artist = QStringLiteral("Artist");
  properties.albumTitle = QStringLiteral("Album");
  data.properties = properties;
  data.timeline = MediaTimeline{10, 1000, 200};
  MediaPlayback playback;
  playback.status = playing ? 4 : 5;
  playback.timelineEnabled = true;
  playback.shuffleEnabled = true;
  playback.repeatEnabled = true;
  playback.prevEnabled = false;
  data.playback = playback;
  return data;
}

struct Harness {
  FakeSource* source = nullptr;
  std::shared_ptr<MediaSessionService> service;
  std::unique_ptr<Mpris> mpris;

  Harness() {
    auto fake = std::make_unique<FakeSource>();
    source = fake.get();
    service = std::make_shared<MediaSessionService>(std::move(fake));
    mpris = std::make_unique<Mpris>(service, nullptr);
  }

  void publish(const std::vector<MediaSessionData>& sessions, const QString& current = {}) {
    const QString id = current.isEmpty() && !sessions.empty() ? sessions.front().appId : current;
    source->sink_->sessionsChanged(sessions, id, true);
    QCoreApplication::processEvents();  // the sink posts to the GUI thread
  }
};

}  // namespace

TEST(Mpris, StartsWithoutPlayers) {
  Harness h;
  EXPECT_EQ(h.mpris->players()->count(), 0);
  EXPECT_EQ(h.mpris->activePlayer(), nullptr);
}

TEST(Mpris, ListsEverySessionWithItsProperties) {
  Harness h;
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One")),
             player(QStringLiteral("b.exe"), QStringLiteral("Two"), false)});
  ASSERT_EQ(h.mpris->players()->count(), 2);
  MprisPlayer* a = h.mpris->players()->values().at(0);
  MprisPlayer* b = h.mpris->players()->values().at(1);
  EXPECT_EQ(a->identity(), QStringLiteral("a.exe name"));
  EXPECT_EQ(a->trackTitle(), QStringLiteral("One"));
  EXPECT_EQ(a->trackArtist(), QStringLiteral("Artist"));
  EXPECT_EQ(a->trackAlbum(), QStringLiteral("Album"));
  EXPECT_EQ(a->length(), 200);
  EXPECT_TRUE(a->isPlaying());
  EXPECT_EQ(a->playbackState(), MprisPlaybackState::Playing);
  EXPECT_FALSE(a->canGoPrevious());
  EXPECT_TRUE(a->canGoNext());
  EXPECT_TRUE(a->canSeek());
  EXPECT_TRUE(a->canTogglePlaying());
  EXPECT_FALSE(b->isPlaying());
  EXPECT_EQ(b->playbackState(), MprisPlaybackState::Paused);
  EXPECT_EQ(b->trackTitle(), QStringLiteral("Two"));
  EXPECT_NE(a->uniqueId(), b->uniqueId());
  EXPECT_EQ(a->metadata().value(QStringLiteral("xesam:title")).toString(), QStringLiteral("One"));
}

TEST(Mpris, ActivePlayerFollowsTheCurrentSession) {
  Harness h;
  QSignalSpy active(h.mpris.get(), &Mpris::activePlayerChanged);
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One")),
             player(QStringLiteral("b.exe"), QStringLiteral("Two"))},
            QStringLiteral("b.exe"));
  ASSERT_NE(h.mpris->activePlayer(), nullptr);
  EXPECT_EQ(h.mpris->activePlayer()->trackTitle(), QStringLiteral("Two"));
  EXPECT_GE(active.count(), 1);
}

TEST(Mpris, AssigningTheActivePlayerSelectsIt) {
  Harness h;
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One")),
             player(QStringLiteral("b.exe"), QStringLiteral("Two"))});
  ASSERT_EQ(h.mpris->activePlayer()->trackTitle(), QStringLiteral("One"));
  h.mpris->setActivePlayer(h.mpris->players()->values().at(1));
  EXPECT_EQ(h.mpris->activePlayer()->trackTitle(), QStringLiteral("Two"));
}

TEST(Mpris, VanishedSessionRemovesItsPlayer) {
  Harness h;
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One")),
             player(QStringLiteral("b.exe"), QStringLiteral("Two"))});
  h.publish({player(QStringLiteral("b.exe"), QStringLiteral("Two"))});
  ASSERT_EQ(h.mpris->players()->count(), 1);
  EXPECT_EQ(h.mpris->players()->values().at(0)->trackTitle(), QStringLiteral("Two"));
  EXPECT_EQ(h.mpris->activePlayer(), h.mpris->players()->values().at(0));
  h.publish({});
  EXPECT_EQ(h.mpris->players()->count(), 0);
  EXPECT_EQ(h.mpris->activePlayer(), nullptr);
}

TEST(Mpris, TrackChangeEmitsPerPropertySignalsThenPostTrackChanged) {
  Harness h;
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One"))});
  MprisPlayer* p = h.mpris->players()->values().at(0);
  QSignalSpy title(p, &MprisPlayer::trackTitleChanged);
  QSignalSpy post(p, &MprisPlayer::postTrackChanged);
  QSignalSpy artist(p, &MprisPlayer::trackArtistChanged);
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("Next"))});
  EXPECT_EQ(title.count(), 1);
  EXPECT_EQ(artist.count(), 0);
  EXPECT_EQ(post.count(), 1);
  EXPECT_EQ(p, h.mpris->players()->values().at(0));
}

TEST(Mpris, ThumbnailBecomesTheArtUrl) {
  Harness h;
  MediaSessionData data = player(QStringLiteral("a.exe"), QStringLiteral("One"));
  data.properties->thumbnail = QByteArray("\x89PNG\r\n\x1a\n....", 12);
  h.publish({data});
  MprisPlayer* p = h.mpris->players()->values().at(0);
  EXPECT_EQ(p->trackArtUrl(), imageDataUrl(data.properties->thumbnail));
  data.properties->thumbnail.clear();
  h.publish({data});
  EXPECT_TRUE(p->trackArtUrl().isEmpty());
}

TEST(Mpris, CommandsAddressTheirOwnSessionNotTheCurrentOne) {
  Harness h;
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One")),
             player(QStringLiteral("b.exe"), QStringLiteral("Two"))});
  MprisPlayer* b = h.mpris->players()->values().at(1);
  b->togglePlaying();
  b->play();
  b->pause();
  b->stop();
  b->next();
  b->previous();
  b->setPosition(42);
  b->setShuffle(true);
  b->setLoopState(MprisLoopState::Track);
  ASSERT_EQ(h.source->commands.size(), 9u);
  for (const Command& c : h.source->commands) EXPECT_EQ(c.appId, QStringLiteral("b.exe"));
  const std::vector<MediaCommand> expected = {
      MediaCommand::PlayPause, MediaCommand::Play,       MediaCommand::Pause,
      MediaCommand::Stop,      MediaCommand::Next,       MediaCommand::Previous,
      MediaCommand::Seek,      MediaCommand::SetShuffle, MediaCommand::SetRepeat};
  for (size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(h.source->commands[i].command, expected[i]);
  EXPECT_EQ(h.source->commands[6].value, 42);
  EXPECT_EQ(h.source->commands[7].value, 1);
  EXPECT_EQ(h.source->commands[8].value, 1);
}

TEST(Mpris, UnsupportedControlsDoNothing) {
  Harness h;
  MediaSessionData data = player(QStringLiteral("a.exe"), QStringLiteral("One"));
  data.playback->timelineEnabled = false;
  data.playback->shuffleEnabled = false;
  data.playback->repeatEnabled = false;
  h.publish({data});
  MprisPlayer* p = h.mpris->players()->values().at(0);
  EXPECT_FALSE(p->canSeek());
  EXPECT_FALSE(p->shuffleSupported());
  p->setPosition(5);
  p->setShuffle(true);
  p->setLoopState(MprisLoopState::Playlist);
  EXPECT_TRUE(h.source->commands.empty());
}

TEST(Mpris, SharesTheServiceWithMedia) {
  Harness h;
  Media media(h.service, nullptr);
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One"))});
  EXPECT_EQ(media.title(), QStringLiteral("One"));
  EXPECT_EQ(h.mpris->activePlayer()->trackTitle(), QStringLiteral("One"));
}

TEST(Mpris, IsUsableFromQml) {
  Harness h;
  h.publish({player(QStringLiteral("a.exe"), QStringLiteral("One")),
             player(QStringLiteral("b.exe"), QStringLiteral("Two"), false)});
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("mpris"), h.mpris.get());
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import wabar.services
QtObject {
    readonly property list<MprisPlayer> list: mpris.players.values
    readonly property MprisPlayer active: list.find(p => p.identity === "b.exe name") ?? list[0] ?? null
    property int count: list.length
    property string activeTitle: active ? active.trackTitle : ""
    property bool paused: active.playbackState === MprisPlaybackState.Paused
    property bool noneLooping: list[0].loopState === MprisLoopState.None
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_EQ(root->property("count").toInt(), 2);
  EXPECT_EQ(root->property("activeTitle").toString(), QStringLiteral("Two"));
  EXPECT_TRUE(root->property("paused").toBool());
  EXPECT_TRUE(root->property("noneLooping").toBool());
}

TEST(Mpris, QuickshellServicesMprisModuleForwardsTheTypes) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import Quickshell.Services.Mpris
QtObject {
    property int track: MprisLoopState.Track
    property int playlist: MprisLoopState.Playlist
    property int playing: MprisPlaybackState.Playing
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_EQ(root->property("track").toInt(), 1);
  EXPECT_EQ(root->property("playlist").toInt(), 2);
  EXPECT_EQ(root->property("playing").toInt(), 1);
}

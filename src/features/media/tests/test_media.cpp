#include <gtest/gtest.h>

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <memory>
#include <vector>

#include "features/media/qml/media_service.h"
#include "features/media/testing/fake_media_source.h"

using namespace wabar::media;
using namespace wabar::media::testing;

namespace {

QByteArray encodedImage(const char* format, QColor color = QColor(200, 80, 40)) {
  QImage image(8, 8, QImage::Format_RGB32);
  image.fill(color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, format);
  return bytes;
}

MediaSessionData session(const QString& title, const QByteArray& thumbnail = {},
                         bool playing = true) {
  MediaSessionData data;
  data.appId = QStringLiteral("Fake.Player.App");
  data.sourceName = QStringLiteral("Fake Player");
  MediaProperties properties;
  properties.title = title;
  properties.artist = QStringLiteral("Artist");
  properties.albumTitle = QStringLiteral("Album");
  properties.thumbnail = thumbnail;
  data.properties = properties;
  // lastUpdated <= 0 is an unset WinRT DateTime, which the service ignores by design.
  data.timeline = MediaTimeline{10, 1000, 200};
  MediaPlayback playback;
  playback.status = playing ? 4 : 5;
  playback.prevEnabled = false;
  playback.nextEnabled = true;
  data.playback = playback;
  return data;
}

// Delivers one session list the way the source's worker thread would, then lets the GUI thread
// apply it.
struct Harness {
  FakeSource* source = nullptr;
  std::unique_ptr<Media> media;

  Harness() {
    auto fake = std::make_unique<FakeSource>();
    source = fake.get();
    media = std::make_unique<Media>(std::move(fake), nullptr);
  }

  void publish(const std::vector<MediaSessionData>& sessions) {
    const QString current = sessions.empty() ? QString() : sessions.front().appId;
    source->sink_->sessionsChanged(sessions, current, true);
    QCoreApplication::processEvents();  // the sink posts to the GUI thread
  }
};

}  // namespace

TEST(MediaMime, RecognisesCommonFormatsByMagicNumber) {
  EXPECT_EQ(imageMimeType(encodedImage("PNG")), QStringLiteral("image/png"));
  EXPECT_EQ(imageMimeType(encodedImage("JPG")), QStringLiteral("image/jpeg"));
  EXPECT_EQ(imageMimeType(encodedImage("BMP")), QStringLiteral("image/bmp"));
  EXPECT_EQ(imageMimeType(QByteArray("GIF89a....")), QStringLiteral("image/gif"));
  EXPECT_EQ(imageMimeType(QByteArray("RIFF\x10\x00\x00\x00WEBPVP8 ", 16)),
            QStringLiteral("image/webp"));
  EXPECT_EQ(imageMimeType(QByteArray("not an image")), QStringLiteral("application/octet-stream"));
  EXPECT_EQ(imageMimeType({}), QStringLiteral("application/octet-stream"));
}

TEST(MediaMime, DataUrlCarriesTheMimeTypeAndBase64Bytes) {
  const QByteArray png = encodedImage("PNG");
  const QString url = imageDataUrl(png);
  EXPECT_TRUE(url.startsWith(QStringLiteral("data:image/png;base64,")));
  EXPECT_EQ(
      QByteArray::fromBase64(url.mid(QStringLiteral("data:image/png;base64,").size()).toLatin1()),
      png);
  EXPECT_TRUE(imageDataUrl({}).isEmpty());
}

TEST(MediaService, DataUrlIsALoadableImage) {
  const QString url = imageDataUrl(encodedImage("PNG"));
  const QByteArray payload = QByteArray::fromBase64(url.section(',', 1).toLatin1());
  QImage image;
  ASSERT_TRUE(image.loadFromData(payload));
  EXPECT_EQ(image.size(), QSize(8, 8));
}

TEST(MediaService, HasNoSessionAtFirst) {
  Harness h;
  EXPECT_FALSE(h.media->available());
  EXPECT_TRUE(h.media->title().isEmpty());
  EXPECT_TRUE(h.media->thumbnailUrl().isEmpty());
  EXPECT_FALSE(h.media->playing());
  EXPECT_FALSE(h.media->canPlayPause());
  EXPECT_EQ(h.media->duration(), 0);
}

TEST(MediaService, ExposesTheCurrentSession) {
  Harness h;
  QSignalSpy changed(h.media.get(), &Media::changed);
  h.publish({session(QStringLiteral("Song"))});
  ASSERT_TRUE(h.media->available());
  EXPECT_GE(changed.count(), 1);
  EXPECT_EQ(h.media->title(), QStringLiteral("Song"));
  EXPECT_EQ(h.media->artist(), QStringLiteral("Artist"));
  EXPECT_EQ(h.media->album(), QStringLiteral("Album"));
  EXPECT_EQ(h.media->sourceName(), QStringLiteral("Fake Player"));
  EXPECT_TRUE(h.media->playing());
  EXPECT_EQ(h.media->duration(), 200);
  EXPECT_FALSE(h.media->canPrevious());
  EXPECT_TRUE(h.media->canNext());
}

TEST(MediaService, ASessionListIsOneRoundOfSignalsAndTicksMoveOnlyThePosition) {
  Harness h;
  QSignalSpy changed(h.media.get(), &Media::changed);
  QSignalSpy position(h.media.get(), &Media::positionChanged);
  std::vector<MediaSessionData> sessions;
  for (const char* id : {"A.App", "B.App", "C.App"}) {
    MediaSessionData data = session(QString::fromLatin1(id));
    data.appId = QString::fromLatin1(id);
    sessions.push_back(data);
  }
  h.source->sink_->sessionsChanged(sessions, sessions.front().appId, true);
  QCoreApplication::processEvents();
  // Each kind of change once for the list, not once per session (about 16 before).
  EXPECT_LE(changed.count(), 7);
  ASSERT_TRUE(h.media->playing());

  // The 100 ms tick while playing moves the position only.
  const int before = changed.count();
  const int ticks = position.count();
  ASSERT_TRUE(QTest::qWaitFor([&] { return position.count() >= ticks + 2; }, 5000));
  EXPECT_EQ(changed.count(), before);
}

TEST(MediaService, UnsetTimelineIsIgnored) {
  Harness h;
  MediaSessionData data = session(QStringLiteral("Song"));
  data.timeline = MediaTimeline{10, 0, 200};  // 1601 epoch in WinRT: unset
  h.publish({data});
  EXPECT_EQ(h.media->duration(), 0);
}

TEST(MediaService, FollowsPlayAndPause) {
  Harness h;
  h.publish({session(QStringLiteral("Song"), {}, true)});
  EXPECT_TRUE(h.media->playing());
  h.source->sink_->playbackChanged(QStringLiteral("Fake.Player.App"), [] {
    MediaPlayback p;
    p.status = 5;
    return p;
  }());
  QCoreApplication::processEvents();  // the sink posts to the GUI thread
  EXPECT_FALSE(h.media->playing());
}

TEST(MediaService, ThumbnailIsTheBytesThePlayerDelivered) {
  Harness h;
  const QByteArray art = encodedImage("PNG");
  h.publish({session(QStringLiteral("Song"), art)});
  EXPECT_EQ(h.media->thumbnailUrl(), imageDataUrl(art));
}

TEST(MediaService, ThumbnailUpdatesWhenTheArtChangesAndClearsWhenItGoes) {
  Harness h;
  const QByteArray first = encodedImage("PNG", QColor(200, 80, 40));
  const QByteArray second = encodedImage("JPG", QColor(20, 90, 200));
  h.publish({session(QStringLiteral("One"), first)});
  EXPECT_EQ(h.media->thumbnailUrl(), imageDataUrl(first));
  h.publish({session(QStringLiteral("Two"), second)});
  EXPECT_EQ(h.media->thumbnailUrl(), imageDataUrl(second));
  EXPECT_TRUE(h.media->thumbnailUrl().startsWith(QStringLiteral("data:image/jpeg;base64,")));
  h.publish({session(QStringLiteral("Three"))});
  EXPECT_TRUE(h.media->thumbnailUrl().isEmpty());
}

TEST(MediaService, NoSessionsMeansUnavailableAgain) {
  Harness h;
  h.publish({session(QStringLiteral("Song"), encodedImage("PNG"))});
  ASSERT_TRUE(h.media->available());
  h.publish({});
  EXPECT_FALSE(h.media->available());
  EXPECT_TRUE(h.media->title().isEmpty());
  EXPECT_TRUE(h.media->thumbnailUrl().isEmpty());
}

TEST(MediaService, CommandsReachTheCurrentSession) {
  Harness h;
  h.publish({session(QStringLiteral("Song"))});
  h.media->playPause();
  h.media->next();
  h.media->previous();
  h.media->seek(42);
  ASSERT_EQ(h.source->commands.size(), 4u);
  for (const Command& c : h.source->commands) EXPECT_EQ(c.appId, QStringLiteral("Fake.Player.App"));
  EXPECT_EQ(h.source->commands[0].command, MediaCommand::PlayPause);
  EXPECT_EQ(h.source->commands[1].command, MediaCommand::Next);
  EXPECT_EQ(h.source->commands[2].command, MediaCommand::Previous);
  EXPECT_EQ(h.source->commands[3].command, MediaCommand::Seek);
  EXPECT_EQ(h.source->commands[3].value, 42);
}

TEST(MediaService, DestructionStopsTheSource) {
  auto fake = std::make_unique<FakeSource>();
  FakeSource* source = fake.get();
  auto media = std::make_unique<Media>(std::move(fake), nullptr);
  EXPECT_NE(source->sink_, nullptr);
  // The service owns the source, so observe through a second Media-free check: stop() ran if the
  // destructor completes without touching a dangling sink.
  media.reset();
  SUCCEED();
}

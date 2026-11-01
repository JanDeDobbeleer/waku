// ThumbnailStore, the thumb image provider and the Thumbnails singleton. Every file lives in a
// QTemporaryDir; the real cache folder is never touched.
#include <gtest/gtest.h>

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQuickImageResponse>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>

#include "host/qml/thumbnails.h"
#include "host/thumbnail_store.h"

using namespace wabar::host;

namespace {

QImage gradient(int width, int height) {
  QImage image(width, height, QImage::Format_RGB32);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) image.setPixel(x, y, qRgb(x % 256, y % 256, (x + y) % 256));
  }
  return image;
}

QString writePng(const QDir& dir, const QString& name, int width, int height) {
  const QString path = dir.filePath(name);
  EXPECT_TRUE(gradient(width, height).save(path, "PNG"));
  return path;
}

QStringList cacheFiles(const QString& root) {
  return QDir(root).entryList(QDir::Files | QDir::NoDotAndDotDot);
}

struct Fixture {
  Fixture() : sources(QDir(dir.path()).filePath(QStringLiteral("src"))), cache(root()) {
    QDir().mkpath(sources);
  }
  QString root() const { return QDir(dir.path()).filePath(QStringLiteral("cache")); }
  QTemporaryDir dir;
  QString sources;
  ThumbnailStore cache;
  QString png(const QString& name, int w = 800, int h = 400) {
    return writePng(QDir(sources), name, w, h);
  }
};

}  // namespace

TEST(Thumbnails, ScalesKeepingAspectRatio) {
  Fixture f;
  const QImage image = f.cache.get(f.png(QStringLiteral("a.png"), 800, 400), 200);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.width(), 200);
  EXPECT_EQ(image.height(), 100);
}

TEST(Thumbnails, DoesNotEnlargeASmallSource) {
  Fixture f;
  const QImage image = f.cache.get(f.png(QStringLiteral("s.png"), 100, 50), 400);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.size(), QSize(100, 50));
}

TEST(Thumbnails, SecondCallComesFromTheCacheFile) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"));
  const QString file = f.cache.cacheFile(source, 200);
  ASSERT_FALSE(file.isEmpty());
  EXPECT_FALSE(QFileInfo::exists(file));

  ASSERT_FALSE(f.cache.get(source, 200).isNull());
  ASSERT_TRUE(QFileInfo::exists(file));
  const QDateTime written = QFileInfo(file).lastModified();
  const qint64 bytes = QFileInfo(file).size();

  // A second store has no memory cache, so it must read the file and leave it alone.
  ThumbnailStore second(f.root());
  const QImage again = second.get(source, 200);
  ASSERT_FALSE(again.isNull());
  EXPECT_EQ(again.size(), QSize(200, 100));
  EXPECT_EQ(QFileInfo(file).lastModified(), written);
  EXPECT_EQ(QFileInfo(file).size(), bytes);
  EXPECT_EQ(cacheFiles(f.root()).size(), 1);
}

TEST(Thumbnails, UsesTheCacheFileInsteadOfTheSource) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"));
  ASSERT_FALSE(f.cache.get(source, 200).isNull());
  // Overwrite the cache entry with a different image of the same name: the next store returns it,
  // which proves the source was not decoded again.
  const QString file = f.cache.cacheFile(source, 200);
  QImage marker(200, 100, QImage::Format_RGB32);
  marker.fill(Qt::red);
  ASSERT_TRUE(marker.save(file, f.cache.extension().toLatin1().constData(), 100));
  ThumbnailStore second(f.root());
  const QImage image = second.get(source, 200);
  ASSERT_FALSE(image.isNull());
  EXPECT_GT(QColor(image.pixel(100, 50)).red(), 200);
  EXPECT_LT(QColor(image.pixel(100, 50)).green(), 60);
}

TEST(Thumbnails, RegeneratesWhenTheSourceChanges) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"), 800, 400);
  const QString before = f.cache.cacheFile(source, 200);
  ASSERT_FALSE(f.cache.get(source, 200).isNull());

  // Rewrite the source with another size and a later modification time.
  ASSERT_TRUE(gradient(400, 400).save(source, "PNG"));
  QFile touch(source);
  ASSERT_TRUE(touch.open(QIODevice::ReadWrite));
  touch.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime);
  touch.close();

  const QString after = f.cache.cacheFile(source, 200);
  EXPECT_NE(before, after);
  const QImage image = f.cache.get(source, 200);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.size(), QSize(200, 200));
  EXPECT_TRUE(QFileInfo::exists(after));
}

TEST(Thumbnails, DifferentWidthsAreDifferentEntries) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"));
  EXPECT_NE(f.cache.cacheFile(source, 100), f.cache.cacheFile(source, 200));
}

TEST(Thumbnails, MissingSourceGivesANullImage) {
  Fixture f;
  EXPECT_TRUE(f.cache.get(QDir(f.sources).filePath(QStringLiteral("none.png")), 200).isNull());
  EXPECT_TRUE(f.cache.get(QString(), 200).isNull());
  EXPECT_FALSE(f.cache.warm(QDir(f.sources).filePath(QStringLiteral("none.png")), 200));
}

TEST(Thumbnails, CorruptSourceGivesANullImage) {
  Fixture f;
  const QString path = QDir(f.sources).filePath(QStringLiteral("bad.png"));
  QFile bad(path);
  ASSERT_TRUE(bad.open(QIODevice::WriteOnly));
  bad.write("this is not an image");
  bad.close();
  EXPECT_TRUE(f.cache.get(path, 200).isNull());
  EXPECT_TRUE(cacheFiles(f.root()).isEmpty());
}

TEST(Thumbnails, CorruptCacheFileIsRebuilt) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"));
  ASSERT_FALSE(f.cache.get(source, 200).isNull());
  const QString file = f.cache.cacheFile(source, 200);
  QFile broken(file);
  ASSERT_TRUE(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
  broken.write("garbage");
  broken.close();

  ThumbnailStore second(f.root());
  const QImage image = second.get(source, 200);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.size(), QSize(200, 100));
  EXPECT_GT(QFileInfo(file).size(), 7);
}

TEST(Thumbnails, WarmFillsTheFileCache) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"));
  EXPECT_TRUE(f.cache.warm(source, 200));
  EXPECT_TRUE(QFileInfo::exists(f.cache.cacheFile(source, 200)));
  EXPECT_TRUE(f.cache.warm(source, 200));  // second time: already there
}

TEST(Thumbnails, NoTemporaryFileIsLeftBehind) {
  Fixture f;
  ASSERT_FALSE(f.cache.get(f.png(QStringLiteral("a.png")), 200).isNull());
  for (const QString& name : cacheFiles(f.root()))
    EXPECT_FALSE(name.contains(QStringLiteral(".tmp")));
}

TEST(Thumbnails, MemoryOnlyStoreWorksWithoutAFolder) {
  Fixture f;
  ThumbnailStore memory{QString()};
  const QString source = f.png(QStringLiteral("a.png"));
  EXPECT_FALSE(memory.get(source, 200).isNull());
  EXPECT_TRUE(memory.warm(source, 200));
  EXPECT_TRUE(memory.cacheFile(source, 200).isEmpty());
}

TEST(Thumbnails, MemoryCacheEvictsByImageSizeAbove8Mb) {
  // No cache folder: a repeat call shares the pixel buffer only on a memory hit, so equal buffers
  // show a hit and a fresh buffer shows an eviction.
  QTemporaryDir dir;
  ThumbnailStore store{QString()};
  const QDir sources(dir.path());
  // 1000 x 1000 RGB32 is 4 MB. Two fit in 8 MB; the third pushes the oldest out.
  const QString a = writePng(sources, QStringLiteral("a.png"), 1000, 1000);
  const QString b = writePng(sources, QStringLiteral("b.png"), 1000, 1000);
  const QString c = writePng(sources, QStringLiteral("c.png"), 1000, 1000);

  const QImage first = store.get(a, 1000);
  const QImage second = store.get(b, 1000);
  ASSERT_FALSE(first.isNull());
  ASSERT_FALSE(second.isNull());
  EXPECT_EQ(store.get(a, 1000).constBits(), first.constBits());
  EXPECT_EQ(store.get(b, 1000).constBits(), second.constBits());

  const QImage third = store.get(c, 1000);
  ASSERT_FALSE(third.isNull());
  EXPECT_EQ(store.get(c, 1000).constBits(), third.constBits());
  EXPECT_EQ(store.get(b, 1000).constBits(), second.constBits());
  // `a` was evicted: a new call decodes again into a new buffer.
  EXPECT_NE(store.get(a, 1000).constBits(), first.constBits());
}

TEST(Thumbnails, EvictedImageStaysValidWhileReferenced) {
  QTemporaryDir dir;
  ThumbnailStore store{QString()};
  const QDir sources(dir.path());
  const QString a = writePng(sources, QStringLiteral("a.png"), 1000, 1000);
  const QImage held = store.get(a, 1000);
  ASSERT_FALSE(held.isNull());
  const QRgb corner = held.pixel(999, 999);
  // Fill the cache past 8 MB with other images so `a` is evicted.
  for (int i = 0; i < 4; ++i) {
    const QString other = writePng(sources, QStringLiteral("o%1.png").arg(i), 1000, 1000);
    ASSERT_FALSE(store.get(other, 1000).isNull());
  }
  EXPECT_NE(store.get(a, 1000).constBits(), held.constBits());
  EXPECT_EQ(held.size(), QSize(1000, 1000));
  EXPECT_EQ(held.pixel(999, 999), corner);
}

TEST(Thumbnails, PruneKeepsTheNewestFiles) {
  Fixture f;
  const QDateTime now = QDateTime::currentDateTime();
  for (int i = 0; i < 5; ++i) {
    QFile file(QDir(f.root()).filePath(QStringLiteral("e%1.webp").arg(i)));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(1000, 'x'));
    file.setFileTime(now.addSecs(-100 + i), QFileDevice::FileModificationTime);
  }
  EXPECT_EQ(f.cache.prune(3, 1000000), 2);
  const QStringList left = cacheFiles(f.root());
  EXPECT_EQ(left.size(), 3);
  EXPECT_TRUE(left.contains(QStringLiteral("e4.webp")));
  EXPECT_FALSE(left.contains(QStringLiteral("e0.webp")));

  EXPECT_EQ(f.cache.prune(100, 1500), 2);  // byte limit: one 1000-byte file fits
  EXPECT_EQ(cacheFiles(f.root()).size(), 1);
}

TEST(Thumbnails, ResolvesFileUrlsAndEncodedPaths) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a b.png"));
  const QString viaUrl = ThumbnailStore::resolveSource(QUrl::fromLocalFile(source).toString());
  EXPECT_EQ(QFileInfo(viaUrl), QFileInfo(source));
  const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(source));
  EXPECT_EQ(QFileInfo(ThumbnailStore::resolveSource(encoded)), QFileInfo(source));
  EXPECT_TRUE(ThumbnailStore::resolveSource(QStringLiteral("nope")).isEmpty());
}

TEST(Thumbnails, ParsesTheProviderId) {
  QString source;
  int width = 0;
  ThumbnailProvider::parseId(QStringLiteral("C%3A%2Fa%2Fb.png?w=480"), &source, &width);
  EXPECT_EQ(source, QStringLiteral("C%3A%2Fa%2Fb.png"));
  EXPECT_EQ(width, 480);
  ThumbnailProvider::parseId(QStringLiteral("x.png"), &source, &width);
  EXPECT_EQ(source, QStringLiteral("x.png"));
  EXPECT_EQ(width, 0);
}

TEST(Thumbnails, ProviderDeliversAScaledImage) {
  Fixture f;
  const QString source = f.png(QStringLiteral("a.png"));
  ThumbnailProvider provider(std::make_shared<ThumbnailStore>(f.root()));
  const QString id =
      QString::fromLatin1(QUrl::toPercentEncoding(source)) + QStringLiteral("?w=200");
  QQuickImageResponse* response = provider.requestImageResponse(id, QSize());
  QSignalSpy spy(response, &QQuickImageResponse::finished);
  ASSERT_TRUE(spy.wait(10000));
  EXPECT_TRUE(response->errorString().isEmpty());
  std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
  ASSERT_TRUE(texture);
  EXPECT_EQ(texture->textureSize(), QSize(200, 100));
  delete response;
}

TEST(Thumbnails, ProviderReportsAMissingFile) {
  Fixture f;
  ThumbnailProvider provider(std::make_shared<ThumbnailStore>(f.root()));
  QQuickImageResponse* response =
      provider.requestImageResponse(QStringLiteral("no-such-file.png?w=100"), QSize());
  QSignalSpy spy(response, &QQuickImageResponse::finished);
  ASSERT_TRUE(spy.wait(10000));
  EXPECT_FALSE(response->errorString().isEmpty());
  delete response;
}

TEST(Thumbnails, SingletonPrewarmsInTheBackground) {
  Fixture f;
  const QString a = f.png(QStringLiteral("a.png"));
  const QString b = f.png(QStringLiteral("b.png"), 600, 600);
  Thumbnails thumbnails(std::make_shared<ThumbnailStore>(f.root()));
  thumbnails.prewarm({a, QUrl::fromLocalFile(b), QStringLiteral("missing.png")}, 100);
  ASSERT_TRUE(thumbnails.waitForPrewarm());
  EXPECT_TRUE(QFileInfo::exists(thumbnails.store().cacheFile(a, 100)));
  EXPECT_TRUE(QFileInfo::exists(thumbnails.store().cacheFile(b, 100)));
  EXPECT_EQ(cacheFiles(f.root()).size(), 2);
}

TEST(Thumbnails, UrlEncodesThePathAndClampsTheWidth) {
  Fixture f;
  Thumbnails thumbnails(std::make_shared<ThumbnailStore>(QString()));
  const QString url = thumbnails.url(QStringLiteral("C:\\a b\\c.png"), 480);
  EXPECT_TRUE(url.startsWith(QStringLiteral("image://thumb/")));
  EXPECT_TRUE(url.endsWith(QStringLiteral("?w=480")));
  EXPECT_FALSE(url.contains(QLatin1Char(' ')));
  EXPECT_FALSE(url.contains(QLatin1Char('\\')));
  EXPECT_TRUE(thumbnails.url(QStringLiteral("x.png")).endsWith(QStringLiteral("?w=640")));
}

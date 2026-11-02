// The decode size of the wallpaper transition: pure size rules, and a real file decoded scaled.
#include <gtest/gtest.h>

#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>

#include "features/wallpapers/wallpaper_decode.h"

namespace {

using waku::wallpapers::DecodeBudget;
using waku::wallpapers::decodeSize;
using waku::wallpapers::decodeWallpaper;
using waku::wallpapers::WallpaperFit;

DecodeBudget budget(QSize target, WallpaperFit fit = WallpaperFit::Fill) {
  DecodeBudget b;
  b.target = target;
  b.fit = fit;
  return b;
}

}  // namespace

TEST(WallpaperDecode, ALargePictureIsDecodedAtTheCoverOfTheScreen) {
  // 4K picture on a 2880x1920 screen: cover scale 1920/2160, aspect kept.
  const QSize size = decodeSize(QSize(3840, 2160), budget(QSize(2880, 1920)));
  EXPECT_EQ(size, QSize(3413, 1920));
}

TEST(WallpaperDecode, ASmallPictureIsNeverUpscaled) {
  EXPECT_EQ(decodeSize(QSize(800, 600), budget(QSize(2880, 1920))), QSize(800, 600));
  EXPECT_EQ(decodeSize(QSize(2880, 1920), budget(QSize(2880, 1920))), QSize(2880, 1920));
}

TEST(WallpaperDecode, NoTargetMeansNoLimit) {
  EXPECT_EQ(decodeSize(QSize(3840, 2160), budget(QSize())), QSize(3840, 2160));
  EXPECT_EQ(decodeSize(QSize(3840, 2160), budget(QSize(0, 1080))), QSize(3840, 2160));
}

TEST(WallpaperDecode, TileAndCentreKeepTheNaturalSize) {
  for (WallpaperFit fit : {WallpaperFit::Tile, WallpaperFit::Center}) {
    EXPECT_EQ(decodeSize(QSize(3840, 2160), budget(QSize(1000, 800), fit)), QSize(3840, 2160));
  }
}

TEST(WallpaperDecode, TheTranscodeCapAppliesBeforeTheTarget) {
  DecodeBudget b = budget(QSize(), WallpaperFit::Tile);
  b.transcodeLimits = std::make_pair(1000, 1000);
  const QSize capped = decodeSize(QSize(4000, 2000), b);
  EXPECT_LE(capped.width(), 4000);
  EXPECT_LT(capped.width(), 4000);
  EXPECT_EQ(capped.width(), 2 * capped.height());
}

TEST(WallpaperDecode, ADegenerateSizeIsReturnedAsIs) {
  EXPECT_EQ(decodeSize(QSize(0, 0), budget(QSize(100, 100))), QSize(0, 0));
}

TEST(WallpaperDecode, AFileIsDecodedScaledAndNeverAtFullSize) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("big.png"));
  QImage big(1600, 1200, QImage::Format_RGB32);
  big.fill(Qt::blue);
  ASSERT_TRUE(big.save(path));

  const QImage scaled = decodeWallpaper(path, budget(QSize(400, 300)), true);
  ASSERT_FALSE(scaled.isNull());
  EXPECT_EQ(scaled.size(), QSize(400, 300));
  EXPECT_LT(scaled.sizeInBytes(), big.sizeInBytes());

  // Tile keeps the file size; a smaller file is not upscaled.
  EXPECT_EQ(decodeWallpaper(path, budget(QSize(400, 300), WallpaperFit::Tile), true).size(),
            QSize(1600, 1200));
  EXPECT_EQ(decodeWallpaper(path, budget(QSize(4000, 3000)), true).size(), QSize(1600, 1200));
}

TEST(WallpaperDecode, AnUnreadableFileGivesANullImage) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  EXPECT_TRUE(
      decodeWallpaper(dir.filePath(QStringLiteral("missing.png")), budget(QSize(100, 100)), true)
          .isNull());
}

// EXIF orientation 6: stored 1600x900, shown 900x1600. Qt may apply a scaled size before or after
// the rotation; the decoded picture must come out portrait and unstretched either way.
TEST(WallpaperDecode, ARotatedJpegIsScaledInItsDisplayedOrientation) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("rotated.jpg"));
  QImage stored(1600, 900, QImage::Format_RGB32);
  stored.fill(Qt::darkGreen);
  QImageWriter writer(path);
  ASSERT_TRUE(writer.supportsOption(QImageIOHandler::ImageTransformation));
  writer.setTransformation(QImageIOHandler::TransformationRotate90);
  ASSERT_TRUE(writer.write(stored)) << writer.errorString().toStdString();

  const QImage full = decodeWallpaper(path, budget(QSize()), true);
  ASSERT_FALSE(full.isNull());
  ASSERT_LT(full.width(), full.height());  // the orientation was written and honoured
  EXPECT_EQ(full.size(), QSize(900, 1600));

  const DecodeBudget small = budget(QSize(300, 300));
  const QImage scaled = decodeWallpaper(path, small, true);
  ASSERT_FALSE(scaled.isNull());
  EXPECT_LT(scaled.width(), scaled.height());
  const QSize cover = decodeSize(full.size(), small);
  EXPECT_LE(scaled.width(), cover.width() + 1);
  EXPECT_LE(scaled.height(), cover.height() + 1);
  EXPECT_LT(scaled.sizeInBytes(), full.sizeInBytes());
  const double fullAspect = static_cast<double>(full.width()) / full.height();
  const double scaledAspect = static_cast<double>(scaled.width()) / scaled.height();
  EXPECT_NEAR(scaledAspect / fullAspect, 1.0, 0.01);
}

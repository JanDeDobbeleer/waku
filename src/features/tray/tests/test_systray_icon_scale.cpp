#include <gtest/gtest.h>

#include <QColor>
#include <QImage>

#include "features/tray/icon_scale.h"

using waku::tray::lanczosScaleIcon;
using waku::tray::scaledTrayIconRgba;
using waku::tray::scaleTrayIcon;
using waku::tray::sharpen;
using waku::tray::TrayIconData;
using waku::tray::trayIconImage;

namespace {

QImage solid(int size, QColor color) {
  QImage image(size, size, QImage::Format_RGBA8888);
  image.fill(color);
  return image;
}

}  // namespace

TEST(TrayIconScale, ThirtyTwoIsReturnedUnchanged) {
  const QImage image = solid(32, QColor(10, 20, 30, 255));
  EXPECT_EQ(scaleTrayIcon(image, 32), image);
}

TEST(TrayIconScale, SolidColourSurvivesUpscale) {
  const QImage scaled = scaleTrayIcon(solid(16, QColor(200, 100, 50, 255)), 32);
  ASSERT_EQ(scaled.size(), QSize(32, 32));
  for (int y = 0; y < 32; y += 7) {
    for (int x = 0; x < 32; x += 5) EXPECT_EQ(scaled.pixelColor(x, y), QColor(200, 100, 50, 255));
  }
}

TEST(TrayIconScale, TransparentPixelsKeepZeroAlpha) {
  const QImage scaled = lanczosScaleIcon(solid(48, QColor(0, 0, 0, 0)), 32, 32);
  EXPECT_EQ(scaled.pixelColor(10, 10).alpha(), 0);
}

TEST(TrayIconScale, SharpenKeepsBorderAndOvershootsAtEdges) {
  QImage image(8, 8, QImage::Format_RGBA8888);
  image.fill(QColor(100, 100, 100, 255));
  for (int y = 0; y < 8; ++y) {
    for (int x = 4; x < 8; ++x) image.setPixelColor(x, y, QColor(200, 200, 200, 255));
  }
  const QImage out = sharpen(image);
  EXPECT_EQ(out.pixelColor(0, 3), image.pixelColor(0, 3));  // border untouched
  EXPECT_LT(out.pixelColor(3, 3).red(), 100);               // dark side undershoots
  EXPECT_GT(out.pixelColor(4, 3).red(), 200);               // bright side overshoots
}

namespace {

// An icon whose pixels vary, as raw RGBA the way the host delivers it.
TrayIconData gradientIcon(int size) {
  TrayIconData data;
  data.iconWidth = data.iconHeight = size;
  data.iconRgba.resize(qsizetype{4} * size * size);
  for (int i = 0; i < size * size; ++i) {
    char* px = data.iconRgba.data() + i * 4;
    px[0] = static_cast<char>(i % 251);
    px[1] = static_cast<char>((i * 7) % 253);
    px[2] = static_cast<char>((i * 13) % 255);
    px[3] = static_cast<char>(128 + i % 128);
  }
  return data;
}

}  // namespace

TEST(TrayIconScale, TheHostsScaledBytesGiveTheSameImageAsScalingOnTheGuiThread) {
  for (const int size : {16, 24, 48, 64, 256}) {
    TrayIconData data = gradientIcon(size);
    const QImage onGui = trayIconImage(data);
    data.scaledRgba = scaledTrayIconRgba(data);
    ASSERT_EQ(data.scaledRgba.size(), 32 * 32 * 4) << size;
    const QImage fromHost = trayIconImage(data);
    ASSERT_EQ(fromHost.size(), QSize(32, 32)) << size;
    EXPECT_EQ(fromHost.convertToFormat(QImage::Format_RGBA8888),
              onGui.convertToFormat(QImage::Format_RGBA8888))
        << size;
  }
}

TEST(TrayIconScale, AThirtyTwoPixelIconNeedsNoScaledBytes) {
  const TrayIconData data = gradientIcon(32);
  EXPECT_TRUE(scaledTrayIconRgba(data).isEmpty());
  EXPECT_EQ(trayIconImage(data).size(), QSize(32, 32));
  EXPECT_TRUE(scaledTrayIconRgba(TrayIconData{}).isEmpty());
  EXPECT_TRUE(trayIconImage(TrayIconData{}).isNull());
}

TEST(TrayIconScale, TheModelUsesTheScaledBytesWhenThereAreSome) {
  TrayIconData data = gradientIcon(64);
  data.scaledRgba = QByteArray(32 * 32 * 4, '\x40');  // not what scaling would give
  const QImage image = trayIconImage(data);
  EXPECT_EQ(image.pixelColor(5, 5), QColor(0x40, 0x40, 0x40, 0x40));
}

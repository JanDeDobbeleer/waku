#include <gtest/gtest.h>

#include <QColor>
#include <QImage>

#include "widgets/systray/icon_scale.h"

using yasb::widgets::lanczosResize;
using yasb::widgets::scaleTrayIcon;
using yasb::widgets::sharpen;

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
  const QImage scaled = lanczosResize(solid(48, QColor(0, 0, 0, 0)), 32, 32);
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

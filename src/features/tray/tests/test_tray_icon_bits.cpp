// The pixel conversions of icon bitmaps (icon_bits.h) on hand-made GetDIBits buffers.
#include <gtest/gtest.h>

#include <QByteArray>

#include "features/tray/icon_bits.h"
#include "features/tray/icon_scale.h"

using namespace waku::tray;

namespace {

// One BGRA (or RGBA) pixel repeated `count` times.
QByteArray pixels(int count, unsigned char c0, unsigned char c1, unsigned char c2,
                  unsigned char c3) {
  QByteArray bytes;
  for (int i = 0; i < count; ++i) {
    bytes.append(static_cast<char>(c0));
    bytes.append(static_cast<char>(c1));
    bytes.append(static_cast<char>(c2));
    bytes.append(static_cast<char>(c3));
  }
  return bytes;
}

unsigned char at(const QByteArray& bytes, qsizetype index) {
  return static_cast<unsigned char>(bytes.at(index));
}

TrayIconData withPixels(const QByteArray& rgba, int width, int height) {
  TrayIconData data;
  data.iconWidth = width;
  data.iconHeight = height;
  data.iconRgba = rgba;
  return data;
}

}  // namespace

TEST(TrayIconBits, AlphaIconSwapsChannelsAndKeepsItsAlpha) {
  // B=10 G=20 R=30 A=200; the mask is ignored when the colour bitmap has alpha.
  const QByteArray color = pixels(4, 10, 20, 30, 200);
  const QByteArray mask = pixels(4, 255, 255, 255, 0);
  const QByteArray rgba = colorIconToRgba(color, mask, 2, 2);
  ASSERT_EQ(rgba.size(), 16);
  EXPECT_EQ(at(rgba, 0), 30);
  EXPECT_EQ(at(rgba, 1), 20);
  EXPECT_EQ(at(rgba, 2), 10);
  EXPECT_EQ(at(rgba, 3), 200);
}

TEST(TrayIconBits, ClassicIconWithZeroAlphaTakesTransparencyFromTheMask) {
  // A 24 bpp icon read as 32 bpp: every alpha byte is 0. Pixel 0 is masked out, pixel 1 shows.
  QByteArray color = pixels(1, 0, 0, 255, 0) + pixels(1, 0, 255, 0, 0);
  QByteArray mask = pixels(1, 255, 255, 255, 0) + pixels(1, 0, 0, 0, 0);
  const QByteArray rgba = colorIconToRgba(color, mask, 2, 1);
  ASSERT_EQ(rgba.size(), 8);
  EXPECT_EQ(at(rgba, 3), 0);
  EXPECT_EQ(at(rgba, 4), 0);
  EXPECT_EQ(at(rgba, 5), 255);
  EXPECT_EQ(at(rgba, 7), 255);
  EXPECT_FALSE(trayIconPixelsProblem(withPixels(rgba, 2, 1)));
}

TEST(TrayIconBits, ZeroAlphaWithoutAMaskIsOpaque) {
  const QByteArray rgba = colorIconToRgba(pixels(4, 1, 2, 3, 0), QByteArray(), 2, 2);
  ASSERT_EQ(rgba.size(), 16);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(at(rgba, i * 4 + 3), 255);
}

TEST(TrayIconBits, TranslucentPixelsSurviveConversionAndScaling) {
  // Low alpha (anti-aliased or premultiplied-looking pixels) must not turn into a blank icon.
  const QByteArray rgba = colorIconToRgba(pixels(16 * 16, 8, 8, 8, 16), QByteArray(), 16, 16);
  ASSERT_EQ(rgba.size(), 16 * 16 * 4);
  EXPECT_EQ(at(rgba, 3), 16);
  EXPECT_FALSE(trayIconPixelsProblem(withPixels(rgba, 16, 16)));

  QImage source(reinterpret_cast<const uchar*>(rgba.constData()), 16, 16, 16 * 4,
                QImage::Format_RGBA8888);
  const QImage scaled = scaleTrayIcon(source.copy());
  ASSERT_FALSE(scaled.isNull());
  bool anyVisible = false;
  for (int y = 0; y < scaled.height() && !anyVisible; ++y) {
    for (int x = 0; x < scaled.width(); ++x) {
      if (qAlpha(scaled.pixel(x, y)) > 0) {
        anyVisible = true;
        break;
      }
    }
  }
  EXPECT_TRUE(anyVisible);
}

TEST(TrayIconBits, ShortBuffersGiveNothing) {
  EXPECT_TRUE(colorIconToRgba(pixels(3, 1, 2, 3, 4), QByteArray(), 2, 2).isEmpty());
  EXPECT_TRUE(colorIconToRgba(QByteArray(), QByteArray(), 0, 0).isEmpty());
  EXPECT_TRUE(monochromeIconToRgba(pixels(4, 0, 0, 0, 0), 2, 2).isEmpty());
}

TEST(TrayIconBits, MonochromeIconUsesTheStackedMasks) {
  // 4x1: AND row then XOR row. Pixels: black, white, transparent, inverted.
  const QByteArray andRow = pixels(1, 0, 0, 0, 0) + pixels(1, 0, 0, 0, 0) +
                            pixels(1, 255, 255, 255, 0) + pixels(1, 255, 255, 255, 0);
  const QByteArray xorRow = pixels(1, 0, 0, 0, 0) + pixels(1, 255, 255, 255, 0) +
                            pixels(1, 0, 0, 0, 0) + pixels(1, 255, 255, 255, 0);
  const QByteArray rgba = monochromeIconToRgba(andRow + xorRow, 4, 1);
  ASSERT_EQ(rgba.size(), 16);
  EXPECT_EQ(at(rgba, 0), 0);
  EXPECT_EQ(at(rgba, 3), 255);
  EXPECT_EQ(at(rgba, 4), 255);
  EXPECT_EQ(at(rgba, 7), 255);
  EXPECT_EQ(at(rgba, 11), 0);
  EXPECT_EQ(at(rgba, 12), 255);
  EXPECT_EQ(at(rgba, 15), 255);
}

TEST(TrayIconBits, ProblemNamesEmptyShortAndFullyTransparentPixels) {
  EXPECT_EQ(trayIconPixelsProblem(TrayIconData()), QStringLiteral("no icon pixels"));
  EXPECT_EQ(trayIconPixelsProblem(withPixels(QByteArray(8, 'x'), 2, 2)),
            QStringLiteral("8 bytes of pixels for a 2x2 icon"));
  EXPECT_EQ(trayIconPixelsProblem(withPixels(pixels(4, 255, 255, 255, 0), 2, 2)),
            QStringLiteral("all 2x2 pixels are fully transparent"));
  EXPECT_FALSE(trayIconPixelsProblem(withPixels(pixels(4, 0, 0, 0, 1), 2, 2)));
}

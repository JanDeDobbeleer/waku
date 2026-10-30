// The image format plugins a shell relies on (wallpapers are often .webp). `deploy` and the test
// build copy them next to the executable; Qt reports "Unsupported image format" without them.
#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <cstdlib>

namespace {

// A small gradient: a flat colour takes a special path in libwebp's lossless coder.
QImage gradient() {
  QImage image(32, 16, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(x * 8, y * 16, (x + y) * 5));
    }
  }
  return image;
}

QByteArray encodeWebp(const QImage& image, int quality) {
  QByteArray bytes;
  QBuffer out(&bytes);
  if (!out.open(QIODevice::WriteOnly)) return {};
  QImageWriter writer(&out, "webp");
  writer.setQuality(quality);
  if (!writer.write(image)) return {};
  return bytes;
}

}  // namespace

TEST(ImageFormats, WebpPluginIsAvailable) {
  EXPECT_TRUE(QImageReader::supportedImageFormats().contains("webp"));
  EXPECT_TRUE(QImageWriter::supportedImageFormats().contains("webp"));
}

// Lossless: the decoded pixels equal the source.
TEST(ImageFormats, DecodesLosslessWebp) {
  const QImage source = gradient();
  QByteArray bytes = encodeWebp(source, 100);
  ASSERT_GT(bytes.size(), 12);
  EXPECT_EQ(bytes.left(4), QByteArray("RIFF"));
  EXPECT_EQ(bytes.mid(8, 4), QByteArray("WEBP"));

  QBuffer in(&bytes);
  ASSERT_TRUE(in.open(QIODevice::ReadOnly));
  QImageReader reader(&in);
  EXPECT_EQ(reader.format(), QByteArray("webp"));
  EXPECT_EQ(reader.size(), source.size());
  const QImage image = reader.read();
  ASSERT_FALSE(image.isNull()) << reader.errorString().toStdString();
  EXPECT_EQ(image.size(), source.size());
  EXPECT_EQ(image.convertToFormat(QImage::Format_ARGB32), source);
}

// Lossy: the size survives and the colours stay close.
TEST(ImageFormats, DecodesLossyWebp) {
  const QImage source = gradient();
  QByteArray bytes = encodeWebp(source, 75);
  ASSERT_GT(bytes.size(), 12);

  QImage image;
  ASSERT_TRUE(image.loadFromData(bytes, "webp"));
  ASSERT_EQ(image.size(), source.size());
  const QColor a = image.pixelColor(20, 10);
  const QColor b = source.pixelColor(20, 10);
  EXPECT_LE(std::abs(a.red() - b.red()), 24);
  EXPECT_LE(std::abs(a.green() - b.green()), 24);
  EXPECT_LE(std::abs(a.blue() - b.blue()), 24);
}

TEST(ImageFormats, KeepsTheOtherFormats) {
  const QList<QByteArray> formats = QImageReader::supportedImageFormats();
  for (const char* name : {"png", "jpg", "gif", "svg"}) {
    EXPECT_TRUE(formats.contains(name)) << name;
  }
}

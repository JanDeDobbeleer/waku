#include "features/color_scheme/image_source.h"

#include <QImageReader>
#include <QSize>
#include <algorithm>
#include <vector>

#include "features/color_scheme/quantizer.h"

namespace wabar::color_scheme {

namespace {

constexpr int kSeedImageSide = 128;
constexpr int kSeedColorCount = 128;

QImage toArgb32(const QImage& image) { return image.convertToFormat(QImage::Format_ARGB32); }

}  // namespace

QImage readImageScaled(const QString& path, int maxSide, QString* error) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  if (size.isValid() && maxSide > 0 && (size.width() > maxSide || size.height() > maxSide)) {
    QSize scaled = size;
    scaled.scale(maxSide, maxSide, Qt::KeepAspectRatio);
    reader.setScaledSize(scaled);
  }
  QImage image = reader.read();
  if (image.isNull()) {
    if (error != nullptr) *error = reader.errorString();
    return {};
  }
  // Formats that cannot scale while decoding come back full size.
  if (maxSide > 0 && (image.width() > maxSide || image.height() > maxSide)) {
    image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::FastTransformation);
  }
  return image;
}

std::optional<Argb> seedColorFromImage(const QImage& source) {
  if (source.isNull()) return std::nullopt;
  QImage image = source;
  if (image.width() > kSeedImageSide || image.height() > kSeedImageSide) {
    image =
        image.scaled(kSeedImageSide, kSeedImageSide, Qt::KeepAspectRatio, Qt::FastTransformation);
  }
  image = toArgb32(image);

  std::vector<Argb> pixels;
  pixels.reserve(static_cast<size_t>(image.width()) * static_cast<size_t>(image.height()));
  for (int y = 0; y < image.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) pixels.push_back(static_cast<Argb>(line[x]));
  }

  const ColorCounts counts = quantizeCelebi(pixels, kSeedColorCount);
  ScoreOptions options;
  options.desired = 1;
  const std::vector<Argb> ranked = rankedSuggestions(counts, options);
  return ranked.empty() ? options.fallback : ranked.front();
}

double meanLuminance(const QImage& source) {
  if (source.isNull()) return 0.0;
  const QImage image = toArgb32(source);
  double sum = 0.0;
  qint64 count = 0;
  for (int y = 0; y < image.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = line[x];
      if (qAlpha(pixel) != 255) continue;
      sum += 0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) + 0.0722 * qBlue(pixel);
      ++count;
    }
  }
  return count == 0 ? 0.0 : sum / (static_cast<double>(count) * 255.0);
}

}  // namespace wabar::color_scheme

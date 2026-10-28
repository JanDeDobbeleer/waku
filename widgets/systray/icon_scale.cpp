#include "widgets/systray/icon_scale.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yasb::widgets {
namespace {

constexpr int kPrecisionBits = 32 - 8 - 2;  // Pillow PRECISION_BITS for 8-bit images
constexpr double kPi = 3.14159265358979323846;

double sinc(double x) {
  if (x == 0.0) return 1.0;
  x *= kPi;
  return std::sin(x) / x;
}

double lanczos3(double x) {
  if (x > -3.0 && x < 3.0) return sinc(x) * sinc(x / 3.0);
  return 0.0;
}

struct Coeffs {
  std::vector<int> bounds;   // per output pixel: first input index, count
  std::vector<int> weights;  // fixed point, `maxCount` per output pixel
  int maxCount = 0;
};

// Pillow precompute_coeffs + normalize_coeffs_8bpc.
Coeffs precompute(int inSize, int outSize) {
  Coeffs result;
  const double scale = static_cast<double>(inSize) / outSize;
  const double filterScale = std::max(scale, 1.0);
  const double support = 3.0 * filterScale;
  result.maxCount = static_cast<int>(std::ceil(support)) * 2 + 1;
  result.bounds.assign(static_cast<size_t>(outSize) * 2, 0);
  result.weights.assign(static_cast<size_t>(outSize) * result.maxCount, 0);
  std::vector<double> k(static_cast<size_t>(result.maxCount));
  const double ss = 1.0 / filterScale;
  for (int xx = 0; xx < outSize; ++xx) {
    const double center = (xx + 0.5) * scale;
    int xmin = static_cast<int>(center - support + 0.5);
    if (xmin < 0) xmin = 0;
    int xmax = static_cast<int>(center + support + 0.5);
    if (xmax > inSize) xmax = inSize;
    xmax -= xmin;
    double total = 0.0;
    for (int x = 0; x < xmax; ++x) {
      const double w = lanczos3((x + xmin - center + 0.5) * ss);
      k[static_cast<size_t>(x)] = w;
      total += w;
    }
    for (int x = 0; x < xmax; ++x) {
      const double w = total != 0.0 ? k[static_cast<size_t>(x)] / total : k[static_cast<size_t>(x)];
      const double scaled = w * (1 << kPrecisionBits);
      result.weights[static_cast<size_t>(xx) * result.maxCount + x] =
          static_cast<int>(w < 0 ? -0.5 + scaled : 0.5 + scaled);
    }
    result.bounds[static_cast<size_t>(xx) * 2] = xmin;
    result.bounds[static_cast<size_t>(xx) * 2 + 1] = xmax;
  }
  return result;
}

std::uint8_t clip8(std::int64_t value) {
  const std::int64_t v = value >> kPrecisionBits;
  return static_cast<std::uint8_t>(std::clamp<std::int64_t>(v, 0, 255));
}

int mulDiv255(int a, int b) {
  const int tmp = a * b + 128;
  return ((tmp >> 8) + tmp) >> 8;
}

// RGBA -> RGBa (premultiplied), Pillow's convert for resizing.
void premultiply(QImage& image) {
  for (int y = 0; y < image.height(); ++y) {
    std::uint8_t* row = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      std::uint8_t* px = row + x * 4;
      const int alpha = px[3];
      px[0] = static_cast<std::uint8_t>(mulDiv255(px[0], alpha));
      px[1] = static_cast<std::uint8_t>(mulDiv255(px[1], alpha));
      px[2] = static_cast<std::uint8_t>(mulDiv255(px[2], alpha));
    }
  }
}

void unpremultiply(QImage& image) {
  for (int y = 0; y < image.height(); ++y) {
    std::uint8_t* row = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      std::uint8_t* px = row + x * 4;
      const int alpha = px[3];
      if (alpha == 255 || alpha == 0) continue;
      for (int c = 0; c < 3; ++c)
        px[c] = static_cast<std::uint8_t>(std::min(255, (255 * px[c]) / alpha));
    }
  }
}

// Resamples the four interleaved bands along one axis.
QImage resampleAxis(const QImage& in, int newSize, bool horizontal) {
  const int inSize = horizontal ? in.width() : in.height();
  if (inSize == newSize) return in;
  const Coeffs coeffs = precompute(inSize, newSize);
  QImage out(horizontal ? newSize : in.width(), horizontal ? in.height() : newSize,
             QImage::Format_RGBA8888);
  const std::int64_t half = std::int64_t{1} << (kPrecisionBits - 1);
  for (int y = 0; y < out.height(); ++y) {
    std::uint8_t* dst = out.scanLine(y);
    for (int x = 0; x < out.width(); ++x) {
      const int pos = horizontal ? x : y;
      const int first = coeffs.bounds[static_cast<size_t>(pos) * 2];
      const int count = coeffs.bounds[static_cast<size_t>(pos) * 2 + 1];
      const int* weights = &coeffs.weights[static_cast<size_t>(pos) * coeffs.maxCount];
      for (int c = 0; c < 4; ++c) {
        std::int64_t sum = half;
        for (int i = 0; i < count; ++i) {
          const std::uint8_t* src = horizontal ? in.constScanLine(y) + (first + i) * 4
                                               : in.constScanLine(first + i) + x * 4;
          sum += static_cast<std::int64_t>(src[c]) * weights[i];
        }
        dst[x * 4 + c] = clip8(sum);
      }
    }
  }
  return out;
}

}  // namespace

QImage lanczosScaleIcon(const QImage& source, int width, int height) {
  QImage image = source.convertToFormat(QImage::Format_RGBA8888);
  premultiply(image);
  image = resampleAxis(image, width, true);
  image = resampleAxis(image, height, false);
  unpremultiply(image);
  return image;
}

QImage sharpen(const QImage& source) {
  QImage in = source.convertToFormat(QImage::Format_RGBA8888);
  QImage out = in;
  out.detach();
  if (in.width() < 3 || in.height() < 3) return out;
  // ImageFilter.SHARPEN: kernel (-2 -2 -2 / -2 32 -2 / -2 -2 -2), scale 16, offset 0. The border
  // pixels are copied unchanged.
  for (int y = 1; y < in.height() - 1; ++y) {
    const std::uint8_t* above = in.constScanLine(y - 1);
    const std::uint8_t* row = in.constScanLine(y);
    const std::uint8_t* below = in.constScanLine(y + 1);
    std::uint8_t* dst = out.scanLine(y);
    for (int x = 1; x < in.width() - 1; ++x) {
      for (int c = 0; c < 4; ++c) {
        int sum = 0;
        for (int dx = -1; dx <= 1; ++dx) sum += above[(x + dx) * 4 + c] + below[(x + dx) * 4 + c];
        sum += row[(x - 1) * 4 + c] + row[(x + 1) * 4 + c];
        const double value = (32.0 * row[x * 4 + c] - 2.0 * sum) / 16.0 + 0.5;
        dst[x * 4 + c] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
      }
    }
  }
  return out;
}

QImage scaleTrayIcon(const QImage& source, int size) {
  if (source.width() == size && source.height() == size) return source;
  return sharpen(lanczosScaleIcon(source, size, size));
}

}  // namespace yasb::widgets

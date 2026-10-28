#include "widgets/media/lanczos.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace yasb::widgets {
namespace {

constexpr double kSupport = 3.0;

double sinc(double x) {
  if (x == 0.0) return 1.0;
  const double px = 3.14159265358979323846 * x;
  return std::sin(px) / px;
}

double lanczos(double x) {
  if (x > -kSupport && x < kSupport) return sinc(x) * sinc(x / kSupport);
  return 0.0;
}

struct Contribution {
  int first = 0;
  std::vector<float> weights;
};

// Pillow's precompute_coeffs.
std::vector<Contribution> coefficients(int inSize, int outSize) {
  const double scale = static_cast<double>(inSize) / outSize;
  const double filterScale = std::max(scale, 1.0);
  const double support = kSupport * filterScale;
  std::vector<Contribution> result(static_cast<size_t>(outSize));
  for (int i = 0; i < outSize; ++i) {
    const double center = (i + 0.5) * scale;
    const int first = std::max(0, static_cast<int>(center - support + 0.5));
    const int last = std::min(inSize, static_cast<int>(center + support + 0.5));
    Contribution& c = result[static_cast<size_t>(i)];
    c.first = first;
    c.weights.resize(static_cast<size_t>(std::max(0, last - first)));
    double total = 0;
    for (int x = first; x < last; ++x) {
      const double w = lanczos((x + 0.5 - center) / filterScale);
      c.weights[static_cast<size_t>(x - first)] = static_cast<float>(w);
      total += w;
    }
    if (total != 0) {
      for (float& w : c.weights) w = static_cast<float>(w / total);
    }
  }
  return result;
}

}  // namespace

QImage lanczosResize(const QImage& source, int width, int height) {
  if (source.isNull() || width <= 0 || height <= 0) return {};
  const QImage input = source.convertToFormat(QImage::Format_RGBA8888);
  const int inW = input.width();
  const int inH = input.height();

  // Premultiplied float planes: 4 channels per pixel.
  std::vector<float> src(static_cast<size_t>(inW) * inH * 4);
  for (int y = 0; y < inH; ++y) {
    const uchar* row = input.constScanLine(y);
    float* out = &src[static_cast<size_t>(y) * inW * 4];
    for (int x = 0; x < inW; ++x) {
      const float a = row[x * 4 + 3] / 255.0f;
      out[x * 4 + 0] = row[x * 4 + 0] * a;
      out[x * 4 + 1] = row[x * 4 + 1] * a;
      out[x * 4 + 2] = row[x * 4 + 2] * a;
      out[x * 4 + 3] = row[x * 4 + 3];
    }
  }

  // Horizontal pass: inW x inH -> width x inH.
  const std::vector<Contribution> horizontal = coefficients(inW, width);
  std::vector<float> mid(static_cast<size_t>(width) * inH * 4);
  for (int y = 0; y < inH; ++y) {
    const float* row = &src[static_cast<size_t>(y) * inW * 4];
    float* out = &mid[static_cast<size_t>(y) * width * 4];
    for (int x = 0; x < width; ++x) {
      const Contribution& c = horizontal[static_cast<size_t>(x)];
      float acc[4] = {0, 0, 0, 0};
      for (size_t k = 0; k < c.weights.size(); ++k) {
        const float* p = row + (static_cast<size_t>(c.first) + k) * 4;
        for (int ch = 0; ch < 4; ++ch) acc[ch] += p[ch] * c.weights[k];
      }
      for (int ch = 0; ch < 4; ++ch) out[x * 4 + ch] = acc[ch];
    }
  }

  // Vertical pass: width x inH -> width x height, then un-premultiply.
  const std::vector<Contribution> vertical = coefficients(inH, height);
  QImage result(width, height, QImage::Format_RGBA8888);
  auto clamp8 = [](float v) { return static_cast<uchar>(std::clamp(std::lround(v), 0L, 255L)); };
  for (int y = 0; y < height; ++y) {
    const Contribution& c = vertical[static_cast<size_t>(y)];
    uchar* out = result.scanLine(y);
    for (int x = 0; x < width; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (size_t k = 0; k < c.weights.size(); ++k) {
        const float* p = &mid[((static_cast<size_t>(c.first) + k) * width + x) * 4];
        for (int ch = 0; ch < 4; ++ch) acc[ch] += p[ch] * c.weights[k];
      }
      const float alpha = std::clamp(acc[3], 0.0f, 255.0f);
      const float inv = alpha > 0 ? 255.0f / alpha : 0.0f;
      out[x * 4 + 0] = clamp8(acc[0] * inv);
      out[x * 4 + 1] = clamp8(acc[1] * inv);
      out[x * 4 + 2] = clamp8(acc[2] * inv);
      out[x * 4 + 3] = clamp8(alpha);
    }
  }
  return result;
}

}  // namespace yasb::widgets

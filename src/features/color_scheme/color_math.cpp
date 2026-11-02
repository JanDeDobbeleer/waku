#include "features/color_scheme/color_math.h"

#include <algorithm>
#include <cmath>

namespace waku::color_scheme {

namespace {

// Contrast helpers accept a result whose ratio is this close below the request (it rounds to the
// same tenth), and nudge the answer by the tolerance so gamut mapping still reaches the ratio.
constexpr double kContrastRatioEpsilon = 0.04;
constexpr double kLuminanceGamutMapTolerance = 0.4;

double ratioOfYs(double y1, double y2) {
  const double lighterY = std::max(y1, y2);
  const double darkerY = std::min(y1, y2);
  return (lighterY + 5.0) / (darkerY + 5.0);
}

constexpr double kEpsilon = 216.0 / 24389.0;
constexpr double kKappa = 24389.0 / 27.0;

}  // namespace

int redOf(Argb argb) { return static_cast<int>((argb & 0x00ff0000) >> 16); }
int greenOf(Argb argb) { return static_cast<int>((argb & 0x0000ff00) >> 8); }
int blueOf(Argb argb) { return static_cast<int>(argb & 0x000000ff); }
int alphaOf(Argb argb) { return static_cast<int>((argb & 0xff000000) >> 24); }
bool isOpaque(Argb argb) { return alphaOf(argb) == 255; }

Argb argbFromRgb(int red, int green, int blue) {
  return 0xff000000u | (static_cast<Argb>(red & 0xff) << 16) |
         (static_cast<Argb>(green & 0xff) << 8) | static_cast<Argb>(blue & 0xff);
}

Argb argbFromLinrgb(double r, double g, double b) {
  return argbFromRgb(delinearized(r), delinearized(g), delinearized(b));
}

double linearized(double rgbComponent) {
  const double normalized = rgbComponent / 255.0;
  if (normalized <= 0.040449936) return normalized / 12.92 * 100.0;
  return std::pow((normalized + 0.055) / 1.055, 2.4) * 100.0;
}

int delinearized(double linearComponent) {
  const double normalized = linearComponent / 100.0;
  const double encoded = normalized <= 0.0031308 ? normalized * 12.92
                                                 : 1.055 * std::pow(normalized, 1.0 / 2.4) - 0.055;
  return std::clamp(static_cast<int>(std::round(encoded * 255.0)), 0, 255);
}

double yFromLstar(double lstar) {
  if (lstar > 8.0) {
    const double cubeRoot = (lstar + 16.0) / 116.0;
    return cubeRoot * cubeRoot * cubeRoot * 100.0;
  }
  return lstar / kKappa * 100.0;
}

double lstarFromY(double y) {
  const double normalized = y / 100.0;
  if (normalized <= kEpsilon) return kKappa * normalized;
  return 116.0 * std::cbrt(normalized) - 16.0;
}

double lstarFromArgb(Argb argb) {
  const double y = 0.2126 * linearized(redOf(argb)) + 0.7152 * linearized(greenOf(argb)) +
                   0.0722 * linearized(blueOf(argb));
  return lstarFromY(y);
}

Argb argbFromLstar(double lstar) {
  const int component = delinearized(yFromLstar(lstar));
  return argbFromRgb(component, component, component);
}

int signum(double value) { return value < 0 ? -1 : (value == 0 ? 0 : 1); }

double lerp(double start, double stop, double amount) {
  return (1.0 - amount) * start + amount * stop;
}

double sanitizeDegrees(double degrees) {
  if (degrees < 0.0) return std::fmod(degrees, 360.0) + 360.0;
  if (degrees >= 360.0) return std::fmod(degrees, 360.0);
  return degrees;
}

int sanitizeDegreesInt(int degrees) {
  if (degrees < 0) return (degrees % 360) + 360;
  if (degrees >= 360) return degrees % 360;
  return degrees;
}

double diffDegrees(double a, double b) { return 180.0 - std::fabs(std::fabs(a - b) - 180.0); }

double Lab::distanceSquared(const Lab& other) const {
  const double dl = l - other.l;
  const double da = a - other.a;
  const double db = b - other.b;
  return dl * dl + da * da + db * db;
}

Lab labFromArgb(Argb argb) {
  const double rl = linearized(redOf(argb));
  const double gl = linearized(greenOf(argb));
  const double bl = linearized(blueOf(argb));
  const double x = 0.41233895 * rl + 0.35762064 * gl + 0.18051042 * bl;
  const double y = 0.2126 * rl + 0.7152 * gl + 0.0722 * bl;
  const double z = 0.01932141 * rl + 0.11916382 * gl + 0.95034478 * bl;
  auto f = [](double t) { return t > kEpsilon ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0; };
  const double fx = f(x / kWhitePointD65[0]);
  const double fy = f(y / kWhitePointD65[1]);
  const double fz = f(z / kWhitePointD65[2]);
  return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

Argb argbFromLab(const Lab& lab) {
  const double fy = (lab.l + 16.0) / 116.0;
  const double fx = lab.a / 500.0 + fy;
  const double fz = fy - lab.b / 200.0;
  const double fx3 = fx * fx * fx;
  const double fz3 = fz * fz * fz;
  const double xn = fx3 > kEpsilon ? fx3 : (116.0 * fx - 16.0) / kKappa;
  const double yn = lab.l > 8.0 ? fy * fy * fy : lab.l / kKappa;
  const double zn = fz3 > kEpsilon ? fz3 : (116.0 * fz - 16.0) / kKappa;
  const double x = xn * kWhitePointD65[0];
  const double y = yn * kWhitePointD65[1];
  const double z = zn * kWhitePointD65[2];
  return argbFromLinrgb(3.2406 * x - 1.5372 * y - 0.4986 * z, -0.9689 * x + 1.8758 * y + 0.0415 * z,
                        0.0557 * x - 0.2040 * y + 1.0570 * z);
}

double ratioOfTones(double toneA, double toneB) {
  return ratioOfYs(yFromLstar(std::clamp(toneA, 0.0, 100.0)),
                   yFromLstar(std::clamp(toneB, 0.0, 100.0)));
}

double lighter(double tone, double ratio) {
  if (tone < 0.0 || tone > 100.0) return -1.0;
  const double darkY = yFromLstar(tone);
  const double lightY = ratio * (darkY + 5.0) - 5.0;
  const double realContrast = ratioOfYs(lightY, darkY);
  if (realContrast < ratio && std::fabs(realContrast - ratio) > kContrastRatioEpsilon) return -1.0;
  const double value = lstarFromY(lightY) + kLuminanceGamutMapTolerance;
  return (value < 0 || value > 100) ? -1.0 : value;
}

double darker(double tone, double ratio) {
  if (tone < 0.0 || tone > 100.0) return -1.0;
  const double lightY = yFromLstar(tone);
  const double darkY = (lightY + 5.0) / ratio - 5.0;
  const double realContrast = ratioOfYs(lightY, darkY);
  if (realContrast < ratio && std::fabs(realContrast - ratio) > kContrastRatioEpsilon) return -1.0;
  const double value = lstarFromY(darkY) - kLuminanceGamutMapTolerance;
  return (value < 0 || value > 100) ? -1.0 : value;
}

double lighterUnsafe(double tone, double ratio) {
  const double safe = lighter(tone, ratio);
  return safe < 0.0 ? 100.0 : safe;
}

double darkerUnsafe(double tone, double ratio) {
  const double safe = darker(tone, ratio);
  return safe < 0.0 ? 0.0 : safe;
}

}  // namespace waku::color_scheme

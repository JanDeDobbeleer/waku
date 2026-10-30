#include "features/color_scheme/hct.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace wabar::color_scheme {

namespace {

struct Vec3 {
  double a;
  double b;
  double c;
};

Vec3 matrixMultiply(Vec3 in, const double m[3][3]) {
  return {in.a * m[0][0] + in.b * m[0][1] + in.c * m[0][2],
          in.a * m[1][0] + in.b * m[1][1] + in.c * m[1][2],
          in.a * m[2][0] + in.b * m[2][1] + in.c * m[2][2]};
}

constexpr double kScaledDiscountFromLinrgb[3][3] = {
    {0.001200833568784504, 0.002389694492170889, 0.0002795742885861124},
    {0.0005891086651375999, 0.0029785502573438758, 0.0003270666104008398},
    {0.00010146692491640572, 0.0005364214359186694, 0.0032979401770712076},
};

constexpr double kLinrgbFromScaledDiscount[3][3] = {
    {1373.2198709594231, -1100.4251190754821, -7.278681089101213},
    {-271.815969077903, 559.6580465940733, -32.46047482791194},
    {1.9622899599665666, -57.173814538844006, 308.7233197812385},
};

constexpr double kYFromLinrgb[3] = {0.2126, 0.7152, 0.0722};

// Linear value (0..100) of each sRGB channel code at its half step, i.e. where rounding changes the
// channel. The reference library lists these as a table; they are derived here.
const std::array<double, 255>& criticalPlanes() {
  static const std::array<double, 255> planes = [] {
    std::array<double, 255> p{};
    for (int i = 0; i < 255; ++i) p[i] = linearized(i + 0.5);
    return p;
  }();
  return planes;
}

double sanitizeRadians(double angle) { return std::fmod(angle + kPi * 8, kPi * 2); }

double trueDelinearized(double linear) {
  const double normalized = linear / 100.0;
  const double encoded = normalized <= 0.0031308 ? normalized * 12.92
                                                 : 1.055 * std::pow(normalized, 1.0 / 2.4) - 0.055;
  return encoded * 255.0;
}

double chromaticAdaptation(double component) {
  const double af = std::pow(std::fabs(component), 0.42);
  return signum(component) * 400.0 * af / (af + 27.13);
}

double inverseChromaticAdaptation(double adapted) {
  const double abs = std::fabs(adapted);
  const double base = std::max(0.0, 27.13 * abs / (400.0 - abs));
  return signum(adapted) * std::pow(base, 1.0 / 0.42);
}

// Hue of a linear RGB colour in CAM16, in radians.
double hueOf(Vec3 linrgb) {
  const Vec3 scaled = matrixMultiply(linrgb, kScaledDiscountFromLinrgb);
  const double ra = chromaticAdaptation(scaled.a);
  const double ga = chromaticAdaptation(scaled.b);
  const double ba = chromaticAdaptation(scaled.c);
  const double a = (11.0 * ra + -12.0 * ga + ba) / 11.0;
  const double b = (ra + ga - 2.0 * ba) / 9.0;
  return std::atan2(b, a);
}

bool areInCyclicOrder(double a, double b, double c) {
  return sanitizeRadians(b - a) < sanitizeRadians(c - a);
}

double intercept(double source, double mid, double target) {
  return (mid - source) / (target - source);
}

Vec3 lerpPoint(Vec3 source, double t, Vec3 target) {
  return {source.a + (target.a - source.a) * t, source.b + (target.b - source.b) * t,
          source.c + (target.c - source.c) * t};
}

double axisOf(Vec3 v, int axis) { return axis == 0 ? v.a : (axis == 1 ? v.b : v.c); }

Vec3 setCoordinate(Vec3 source, double coordinate, Vec3 target, int axis) {
  return lerpPoint(source, intercept(axisOf(source, axis), coordinate, axisOf(target, axis)),
                   target);
}

bool isBounded(double x) { return 0.0 <= x && x <= 100.0; }

// The nth possible vertex of the polygon where the plane of luminance y cuts the RGB cube, or
// (-1, -1, -1) when that vertex lies outside the cube.
Vec3 nthVertex(double y, int n) {
  const double kr = kYFromLinrgb[0];
  const double kg = kYFromLinrgb[1];
  const double kb = kYFromLinrgb[2];
  const double coordA = n % 4 <= 1 ? 0.0 : 100.0;
  const double coordB = n % 2 == 0 ? 0.0 : 100.0;
  if (n < 4) {
    const double g = coordA;
    const double b = coordB;
    const double r = (y - g * kg - b * kb) / kr;
    if (isBounded(r)) return {r, g, b};
  } else if (n < 8) {
    const double b = coordA;
    const double r = coordB;
    const double g = (y - r * kr - b * kb) / kg;
    if (isBounded(g)) return {r, g, b};
  } else {
    const double r = coordA;
    const double g = coordB;
    const double b = (y - r * kr - g * kg) / kb;
    if (isBounded(b)) return {r, g, b};
  }
  return {-1.0, -1.0, -1.0};
}

// The segment of the polygon's boundary that holds the colour of the target hue.
std::array<Vec3, 2> bisectToSegment(double y, double targetHue) {
  Vec3 left{-1.0, -1.0, -1.0};
  Vec3 right = left;
  double leftHue = 0.0;
  double rightHue = 0.0;
  bool initialized = false;
  bool uncut = true;
  for (int n = 0; n < 12; ++n) {
    const Vec3 mid = nthVertex(y, n);
    if (mid.a < 0) continue;
    const double midHue = hueOf(mid);
    if (!initialized) {
      left = right = mid;
      leftHue = rightHue = midHue;
      initialized = true;
      continue;
    }
    if (uncut || areInCyclicOrder(leftHue, midHue, rightHue)) {
      uncut = false;
      if (areInCyclicOrder(leftHue, targetHue, midHue)) {
        right = mid;
        rightHue = midHue;
      } else {
        left = mid;
        leftHue = midHue;
      }
    }
  }
  return {left, right};
}

int criticalPlaneBelow(double x) { return static_cast<int>(std::floor(x - 0.5)); }
int criticalPlaneAbove(double x) { return static_cast<int>(std::ceil(x - 0.5)); }

// A colour of luminance y and the target hue on the boundary of the cube.
Vec3 bisectToLimit(double y, double targetHue) {
  const auto segment = bisectToSegment(y, targetHue);
  Vec3 left = segment[0];
  double leftHue = hueOf(left);
  Vec3 right = segment[1];
  const auto& planes = criticalPlanes();
  for (int axis = 0; axis < 3; ++axis) {
    if (axisOf(left, axis) == axisOf(right, axis)) continue;
    int lPlane = -1;
    int rPlane = 255;
    if (axisOf(left, axis) < axisOf(right, axis)) {
      lPlane = criticalPlaneBelow(trueDelinearized(axisOf(left, axis)));
      rPlane = criticalPlaneAbove(trueDelinearized(axisOf(right, axis)));
    } else {
      lPlane = criticalPlaneAbove(trueDelinearized(axisOf(left, axis)));
      rPlane = criticalPlaneBelow(trueDelinearized(axisOf(right, axis)));
    }
    for (int i = 0; i < 8; ++i) {
      if (std::abs(rPlane - lPlane) <= 1) break;
      const int mPlane = static_cast<int>(std::floor((lPlane + rPlane) / 2.0));
      const Vec3 mid = setCoordinate(left, planes[static_cast<size_t>(mPlane)], right, axis);
      const double midHue = hueOf(mid);
      if (areInCyclicOrder(leftHue, targetHue, midHue)) {
        right = mid;
        rPlane = mPlane;
      } else {
        left = mid;
        leftHue = midHue;
        lPlane = mPlane;
      }
    }
  }
  return {(left.a + right.a) / 2, (left.b + right.b) / 2, (left.c + right.c) / 2};
}

// Newton iteration on CAM16 J for a colour of the given hue, chroma and luminance. Returns 0 when
// the colour is outside the sRGB gamut.
Argb findResultByJ(double hueRadians, double chroma, double y) {
  const ViewingConditions& vc = defaultViewingConditions();
  double j = std::sqrt(y) * 11.0;
  const double tInnerCoeff = 1 / std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
  const double eHue = 0.25 * (std::cos(hueRadians + 2.0) + 3.8);
  const double p1 = eHue * (50000.0 / 13.0) * vc.nc * vc.ncb;
  const double hSin = std::sin(hueRadians);
  const double hCos = std::cos(hueRadians);
  for (int round = 0; round < 5; ++round) {
    const double jNormalized = j / 100.0;
    const double alpha = chroma == 0.0 || j == 0.0 ? 0.0 : chroma / std::sqrt(jNormalized);
    const double t = std::pow(alpha * tInnerCoeff, 1.0 / 0.9);
    const double ac = vc.aw * std::pow(jNormalized, 1.0 / vc.c / vc.z);
    const double p2 = ac / vc.nbb;
    const double gamma = 23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11 * t * hCos + 108.0 * t * hSin);
    const double a = gamma * hCos;
    const double b = gamma * hSin;
    const double ra = (460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0;
    const double ga = (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0;
    const double ba = (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0;
    const Vec3 scaled{inverseChromaticAdaptation(ra), inverseChromaticAdaptation(ga),
                      inverseChromaticAdaptation(ba)};
    const Vec3 linrgb = matrixMultiply(scaled, kLinrgbFromScaledDiscount);
    if (linrgb.a < 0 || linrgb.b < 0 || linrgb.c < 0) return 0;
    const double fnj =
        kYFromLinrgb[0] * linrgb.a + kYFromLinrgb[1] * linrgb.b + kYFromLinrgb[2] * linrgb.c;
    if (fnj <= 0) return 0;
    if (round == 4 || std::fabs(fnj - y) < 0.002) {
      if (linrgb.a > 100.01 || linrgb.b > 100.01 || linrgb.c > 100.01) return 0;
      return argbFromLinrgb(linrgb.a, linrgb.b, linrgb.c);
    }
    j = j - (fnj - y) * j / (2 * fnj);
  }
  return 0;
}

}  // namespace

const ViewingConditions& defaultViewingConditions() {
  static const ViewingConditions vc = [] {
    ViewingConditions v;
    const double* white = kWhitePointD65;
    v.adaptingLuminance = 200.0 / kPi * yFromLstar(50.0) / 100.0;
    v.backgroundLstar = 50.0;
    v.surround = 2.0;
    v.discountingIlluminant = false;
    const double rgbW[3] = {
        0.401288 * white[0] + 0.650173 * white[1] - 0.051461 * white[2],
        -0.250268 * white[0] + 1.204414 * white[1] + 0.045854 * white[2],
        -0.002079 * white[0] + 0.048952 * white[1] + 0.953127 * white[2],
    };
    const double f = 0.8 + v.surround / 10.0;
    v.c = f >= 0.9 ? lerp(0.59, 0.69, (f - 0.9) * 10.0) : lerp(0.525, 0.59, (f - 0.8) * 10.0);
    double d = f * (1.0 - (1.0 / 3.6) * std::exp((-v.adaptingLuminance - 42.0) / 92.0));
    d = std::clamp(d, 0.0, 1.0);
    v.nc = f;
    for (int i = 0; i < 3; ++i) v.rgbD[i] = d * (100.0 / rgbW[i]) + 1.0 - d;
    const double k = 1.0 / (5.0 * v.adaptingLuminance + 1.0);
    const double k4 = k * k * k * k;
    const double k4f = 1.0 - k4;
    v.fl =
        k4 * v.adaptingLuminance + 0.1 * k4f * k4f * std::pow(5.0 * v.adaptingLuminance, 1.0 / 3.0);
    v.flRoot = std::pow(v.fl, 0.25);
    v.n = yFromLstar(v.backgroundLstar) / white[1];
    v.z = 1.48 + std::sqrt(v.n);
    v.nbb = 0.725 / std::pow(v.n, 0.2);
    v.ncb = v.nbb;
    double rgbA[3];
    for (int i = 0; i < 3; ++i) {
      const double factor = std::pow(v.fl * v.rgbD[i] * rgbW[i] / 100.0, 0.42);
      rgbA[i] = 400.0 * factor / (factor + 27.13);
    }
    v.aw = (40.0 * rgbA[0] + 20.0 * rgbA[1] + rgbA[2]) / 20.0 * v.nbb;
    return v;
  }();
  return vc;
}

Cam camFromArgb(Argb argb) {
  const ViewingConditions& vc = defaultViewingConditions();
  const double rl = linearized(redOf(argb));
  const double gl = linearized(greenOf(argb));
  const double bl = linearized(blueOf(argb));
  const double x = 0.41233895 * rl + 0.35762064 * gl + 0.18051042 * bl;
  const double y = 0.2126 * rl + 0.7152 * gl + 0.0722 * bl;
  const double z = 0.01932141 * rl + 0.11916382 * gl + 0.95034478 * bl;

  const double rc = 0.401288 * x + 0.650173 * y - 0.051461 * z;
  const double gc = -0.250268 * x + 1.204414 * y + 0.045854 * z;
  const double bc = -0.002079 * x + 0.048952 * y + 0.953127 * z;

  const double rd = vc.rgbD[0] * rc;
  const double gd = vc.rgbD[1] * gc;
  const double bd = vc.rgbD[2] * bc;

  const double raf = std::pow(vc.fl * std::fabs(rd) / 100.0, 0.42);
  const double gaf = std::pow(vc.fl * std::fabs(gd) / 100.0, 0.42);
  const double baf = std::pow(vc.fl * std::fabs(bd) / 100.0, 0.42);
  const double ra = signum(rd) * 400.0 * raf / (raf + 27.13);
  const double ga = signum(gd) * 400.0 * gaf / (gaf + 27.13);
  const double ba = signum(bd) * 400.0 * baf / (baf + 27.13);

  const double a = (11.0 * ra + -12.0 * ga + ba) / 11.0;
  const double b = (ra + ga - 2.0 * ba) / 9.0;
  const double u = (20.0 * ra + 20.0 * ga + 21.0 * ba) / 20.0;
  const double p2 = (40.0 * ra + 20.0 * ga + ba) / 20.0;

  const double hue = sanitizeDegrees(std::atan2(b, a) * 180.0 / kPi);
  const double hueRadians = hue * kPi / 180.0;
  const double ac = p2 * vc.nbb;

  const double j = 100.0 * std::pow(ac / vc.aw, vc.c * vc.z);
  const double q = (4.0 / vc.c) * std::sqrt(j / 100.0) * (vc.aw + 4.0) * vc.flRoot;
  const double huePrime = hue < 20.14 ? hue + 360 : hue;
  const double eHue = 0.25 * (std::cos(huePrime * kPi / 180.0 + 2.0) + 3.8);
  const double p1 = 50000.0 / 13.0 * eHue * vc.nc * vc.ncb;
  const double t = p1 * std::sqrt(a * a + b * b) / (u + 0.305);
  const double alpha = std::pow(t, 0.9) * std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
  const double c = alpha * std::sqrt(j / 100.0);
  const double m = c * vc.flRoot;
  const double s = 50.0 * std::sqrt((alpha * vc.c) / (vc.aw + 4.0));
  const double jstar = (1.0 + 100.0 * 0.007) * j / (1.0 + 0.007 * j);
  const double mstar = 1.0 / 0.0228 * std::log(1.0 + 0.0228 * m);
  return {hue, c, j, q, m, s, jstar, mstar * std::cos(hueRadians), mstar * std::sin(hueRadians)};
}

Argb argbFromCam(const Cam& cam) {
  const ViewingConditions& vc = defaultViewingConditions();
  const double alpha =
      (cam.chroma == 0.0 || cam.j == 0.0) ? 0.0 : cam.chroma / std::sqrt(cam.j / 100.0);
  const double t = std::pow(alpha / std::pow(1.64 - std::pow(0.29, vc.n), 0.73), 1.0 / 0.9);
  const double hRad = cam.hue * kPi / 180.0;
  const double eHue = 0.25 * (std::cos(hRad + 2.0) + 3.8);
  const double ac = vc.aw * std::pow(cam.j / 100.0, 1.0 / vc.c / vc.z);
  const double p1 = eHue * (50000.0 / 13.0) * vc.nc * vc.ncb;
  const double p2 = ac / vc.nbb;
  const double hSin = std::sin(hRad);
  const double hCos = std::cos(hRad);
  const double gamma = 23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11.0 * t * hCos + 108.0 * t * hSin);
  const double a = gamma * hCos;
  const double b = gamma * hSin;
  const double ra = (460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0;
  const double ga = (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0;
  const double ba = (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0;
  auto cone = [&](double adapted, double d) {
    const double base = std::max(0.0, (27.13 * std::fabs(adapted)) / (400.0 - std::fabs(adapted)));
    return signum(adapted) * (100.0 / vc.fl) * std::pow(base, 1.0 / 0.42) / d;
  };
  const double rx = cone(ra, vc.rgbD[0]);
  const double gx = cone(ga, vc.rgbD[1]);
  const double bx = cone(ba, vc.rgbD[2]);
  const double x = 1.86206786 * rx - 1.01125463 * gx + 0.14918677 * bx;
  const double y = 0.38752654 * rx + 0.62144744 * gx - 0.00897398 * bx;
  const double z = -0.01584150 * rx - 0.03412294 * gx + 1.04996444 * bx;
  return argbFromLinrgb(3.2406 * x - 1.5372 * y - 0.4986 * z, -0.9689 * x + 1.8758 * y + 0.0415 * z,
                        0.0557 * x - 0.2040 * y + 1.0570 * z);
}

Argb solveToArgb(double hue, double chroma, double lstar) {
  if (chroma < 0.0001 || lstar < 0.0001 || lstar > 99.9999) return argbFromLstar(lstar);
  hue = sanitizeDegrees(hue);
  const double hueRadians = hue / 180 * kPi;
  const double y = yFromLstar(lstar);
  const Argb exact = findResultByJ(hueRadians, chroma, y);
  if (exact != 0) return exact;
  const Vec3 linrgb = bisectToLimit(y, hueRadians);
  return argbFromLinrgb(linrgb.a, linrgb.b, linrgb.c);
}

Hct::Hct(Argb argb) { assign(argb); }

Hct::Hct(double hue, double chroma, double tone) { assign(solveToArgb(hue, chroma, tone)); }

void Hct::assign(Argb argb) {
  argb_ = argb;
  const Cam cam = camFromArgb(argb);
  hue_ = cam.hue;
  chroma_ = cam.chroma;
  tone_ = lstarFromArgb(argb);
}

}  // namespace wabar::color_scheme

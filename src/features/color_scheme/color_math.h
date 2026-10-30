#pragma once

#include <cstdint>

namespace wabar::color_scheme {

// Colour arithmetic shared by the HCT solver, the quantizer and the scheme roles. Re-implemented
// from the public description of material-color-utilities (Apache-2.0, see THIRD_PARTY_NOTICES.md).
// A colour is an opaque-or-not 0xAARRGGBB integer, as in the reference library.

using Argb = std::uint32_t;

inline constexpr double kPi = 3.141592653589793;
inline constexpr double kWhitePointD65[3] = {95.047, 100.0, 108.883};

int redOf(Argb argb);
int greenOf(Argb argb);
int blueOf(Argb argb);
int alphaOf(Argb argb);
bool isOpaque(Argb argb);
Argb argbFromRgb(int red, int green, int blue);
// Linear RGB in 0..100 per channel to sRGB.
Argb argbFromLinrgb(double r, double g, double b);

// sRGB channel 0..255 to linear 0..100, and back (rounded and clamped).
double linearized(double rgbComponent);
int delinearized(double linearComponent);

double yFromLstar(double lstar);
double lstarFromY(double y);
double lstarFromArgb(Argb argb);
Argb argbFromLstar(double lstar);

int signum(double value);
double lerp(double start, double stop, double amount);
double sanitizeDegrees(double degrees);
int sanitizeDegreesInt(int degrees);
// Smallest angle between two hues, 0..180.
double diffDegrees(double a, double b);

struct Lab {
  double l = 0;
  double a = 0;
  double b = 0;
  // Squared Euclidean distance, as the reference quantizer uses it.
  double distanceSquared(const Lab& other) const;
};
Lab labFromArgb(Argb argb);
Argb argbFromLab(const Lab& lab);

// WCAG-style contrast on tones (L*), and the tone that reaches a ratio against a given tone.
// lighter() and darker() return -1 when the ratio cannot be reached.
double ratioOfTones(double toneA, double toneB);
double lighter(double tone, double ratio);
double darker(double tone, double ratio);
double lighterUnsafe(double tone, double ratio);
double darkerUnsafe(double tone, double ratio);

}  // namespace wabar::color_scheme

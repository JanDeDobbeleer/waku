#pragma once

#include <vector>

#include "features/color_scheme/hct.h"

namespace wabar::color_scheme {

// One hue and chroma, sampled at any tone 0..100. The colour at a tone keeps the hue and takes the
// chroma that fits the sRGB gamut at that tone.
class TonalPalette {
 public:
  TonalPalette() : TonalPalette(0.0, 0.0) {}
  TonalPalette(double hue, double chroma);
  explicit TonalPalette(const Hct& hct);

  double hue() const { return hue_; }
  double chroma() const { return chroma_; }
  // The first tone from 50 outwards that can show the requested chroma (or the tone with the most
  // chroma when none can). Android uses it to name a palette.
  const Hct& keyColor() const { return keyColor_; }

  Argb tone(double tone) const;

 private:
  double hue_ = 0;
  double chroma_ = 0;
  Hct keyColor_;
};

// Hue-temperature helper for the Fidelity and Content schemes: the complement and analogous
// colours of a colour, found by warm/cool distance over the 361 hues at the colour's chroma/tone.
class TemperatureCache {
 public:
  explicit TemperatureCache(const Hct& input);

  Hct complement() const;
  // `count` colours centred on the input, spread over `divisions` equal temperature steps.
  std::vector<Hct> analogousColors(int count = 5, int divisions = 12) const;

 private:
  double relativeTemperature(const Hct& hct) const;
  static double rawTemperature(const Hct& hct);
  static bool isBetween(double angle, double a, double b);

  Hct input_;
  // hctsByHue_[i] is the colour at hue i (0..360); temps_[i] its temperature. The last entry of
  // both is the input itself, which sorts among the others for coldest/warmest.
  std::vector<Hct> hctsByHue_;
  std::vector<double> temps_;
  size_t coldest_ = 0;
  size_t warmest_ = 0;
};

}  // namespace wabar::color_scheme

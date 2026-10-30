#pragma once

#include "features/color_scheme/color_math.h"

namespace wabar::color_scheme {

// CAM16 viewing conditions. Only the default (D65, 200/pi * Y(L*50)/100 cd/m2, background L* 50,
// average surround) is used; the struct holds the derived terms CAM16 needs.
struct ViewingConditions {
  double adaptingLuminance = 0;
  double backgroundLstar = 0;
  double surround = 0;
  bool discountingIlluminant = false;
  double n = 0;  // background Y over white-point Y
  double aw = 0;
  double nbb = 0;
  double ncb = 0;
  double c = 0;
  double nc = 0;
  double fl = 0;
  double flRoot = 0;
  double z = 0;
  double rgbD[3] = {0, 0, 0};
};
const ViewingConditions& defaultViewingConditions();

// A colour in the CAM16 appearance space under the default viewing conditions.
struct Cam {
  double hue = 0;
  double chroma = 0;
  double j = 0;
  double q = 0;
  double m = 0;
  double s = 0;
  double jstar = 0;
  double astar = 0;
  double bstar = 0;
};
Cam camFromArgb(Argb argb);
// Back to sRGB, clamped to the gamut. Exact for colours that came from camFromArgb.
Argb argbFromCam(const Cam& cam);

// Finds an sRGB colour with the given hue (degrees), chroma and L*. Where the gamut cannot hold
// the chroma it keeps hue and tone and lowers the chroma.
Argb solveToArgb(double hue, double chroma, double lstar);

// Hue, chroma, tone: CAM16 hue and chroma with CIE L* as the tone. Chroma and tone are those of
// the nearest sRGB colour, so a Hct always names a displayable colour.
class Hct {
 public:
  Hct() : Hct(0xff000000u) {}
  explicit Hct(Argb argb);
  Hct(double hue, double chroma, double tone);

  double hue() const { return hue_; }
  double chroma() const { return chroma_; }
  double tone() const { return tone_; }
  Argb argb() const { return argb_; }

  void setHue(double hue) { *this = Hct(hue, chroma_, tone_); }
  void setChroma(double chroma) { *this = Hct(hue_, chroma, tone_); }
  void setTone(double tone) { *this = Hct(hue_, chroma_, tone); }

 private:
  void assign(Argb argb);

  double hue_ = 0;
  double chroma_ = 0;
  double tone_ = 0;
  Argb argb_ = 0;
};

}  // namespace wabar::color_scheme

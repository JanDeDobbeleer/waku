#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <memory>

#include "features/color_scheme/color_math.h"
#include "features/color_scheme/dynamic_scheme.h"
#include "features/color_scheme/hct.h"
#include "features/color_scheme/image_source.h"
#include "features/color_scheme/qml/color_scheme_service.h"
#include "features/color_scheme/quantizer.h"
#include "features/color_scheme/tonal_palette.h"

// Test vectors come from the test suites of material-color-utilities (Apache-2.0, commit
// bd811e4ca2ab94c4ce3cadc89c19b1820b9704b3: typescript/hct/hct_test.ts, typescript/palettes/
// palettes_test.ts, typescript/quantize/quantizer_celebi_test.ts, typescript/score/score_test.ts,
// cpp/scheme/scheme_monochrome_test.cc), and from the published Material 3 baseline colours.

using namespace wabar::color_scheme;

namespace {

constexpr Argb kRed = 0xffff0000;
constexpr Argb kGreen = 0xff00ff00;
constexpr Argb kBlue = 0xff0000ff;
constexpr Argb kWhite = 0xffffffff;
constexpr Argb kBlack = 0xff000000;
constexpr Argb kBaselineSeed = 0xff6750a4;

QString hex(Argb argb) {
  return QStringLiteral("#%1").arg(argb & 0xffffff, 6, 16, QChar('0')).toUpper();
}

// Compares two colours as #RRGGBB text so a failure prints something readable.
#define EXPECT_COLOR(actual, expectedHex) \
  EXPECT_EQ(hex(actual).toStdString(), std::string(expectedHex))

// ---- Colour maths and CAM16 ---------------------------------------------------------------

TEST(ColorSchemeMath, LstarOfGreyAndExtremes) {
  EXPECT_EQ(argbFromLstar(0.0), kBlack);
  EXPECT_EQ(argbFromLstar(100.0), kWhite);
  EXPECT_EQ(argbFromLstar(50.0), 0xff777777u);
  EXPECT_NEAR(lstarFromArgb(kWhite), 100.0, 1e-3);
  EXPECT_NEAR(lstarFromArgb(0xff777777), 49.9, 0.3);
}

TEST(ColorSchemeMath, LabRoundTrips) {
  for (const Argb argb : {kRed, kGreen, kBlue, 0xff6750a4u, 0xff1e64c8u, 0xff808080u}) {
    EXPECT_EQ(argbFromLab(labFromArgb(argb)), argb);
  }
}

TEST(ColorSchemeMath, ContrastRatios) {
  EXPECT_NEAR(ratioOfTones(0, 100), 21.0, 1e-9);
  EXPECT_NEAR(ratioOfTones(50, 50), 1.0, 1e-9);
  // lighter() and darker() give a tone that reaches the ratio, or -1 when none does.
  const double l = lighter(50.0, 3.0);
  ASSERT_GT(l, 50.0);
  EXPECT_GE(ratioOfTones(l, 50.0), 3.0);
  const double d = darker(50.0, 3.0);
  ASSERT_GE(d, 0.0);
  EXPECT_GE(ratioOfTones(d, 50.0), 3.0);
  EXPECT_EQ(lighter(95.0, 21.0), -1.0);
  EXPECT_EQ(lighterUnsafe(95.0, 21.0), 100.0);
}

TEST(ColorSchemeMath, DegreeHelpers) {
  EXPECT_NEAR(sanitizeDegrees(-30.0), 330.0, 1e-9);
  EXPECT_NEAR(sanitizeDegrees(725.0), 5.0, 1e-9);
  EXPECT_NEAR(diffDegrees(350.0, 10.0), 20.0, 1e-9);
  EXPECT_EQ(sanitizeDegreesInt(-1), 359);
}

struct CamExpectation {
  Argb argb;
  double hue;
  double chroma;
  double j;
  double m;
  double s;
  double q;
};

TEST(ColorSchemeCam, MatchesReferenceValues) {
  const CamExpectation expectations[] = {
      {kRed, 27.408, 113.358, 46.445, 89.494, 91.890, 105.989},
      {kGreen, 142.140, 108.410, 79.332, 85.588, 78.605, 138.520},
      {kBlue, 282.788, 87.231, 25.466, 68.867, 93.675, 78.481},
      {kWhite, 209.492, 2.869, 100.0, 2.265, 12.068, 155.521},
      {kBlack, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
  };
  for (const CamExpectation& e : expectations) {
    const Cam cam = camFromArgb(e.argb);
    SCOPED_TRACE(hex(e.argb).toStdString());
    EXPECT_NEAR(cam.hue, e.hue, 5e-4);
    EXPECT_NEAR(cam.chroma, e.chroma, 5e-4);
    EXPECT_NEAR(cam.j, e.j, 5e-4);
    EXPECT_NEAR(cam.m, e.m, 5e-4);
    EXPECT_NEAR(cam.s, e.s, 5e-4);
    EXPECT_NEAR(cam.q, e.q, 5e-4);
  }
}

TEST(ColorSchemeCam, RoundTripsPrimaries) {
  for (const Argb argb : {kRed, kGreen, kBlue}) EXPECT_EQ(argbFromCam(camFromArgb(argb)), argb);
}

TEST(ColorSchemeCam, DefaultViewingConditions) {
  const ViewingConditions& vc = defaultViewingConditions();
  EXPECT_NEAR(vc.n, 0.184, 5e-4);
  EXPECT_NEAR(vc.aw, 29.981, 5e-4);
  EXPECT_NEAR(vc.nbb, 1.017, 5e-4);
  EXPECT_NEAR(vc.ncb, 1.017, 5e-4);
  EXPECT_NEAR(vc.c, 0.69, 5e-4);
  EXPECT_NEAR(vc.nc, 1.0, 5e-4);
  EXPECT_NEAR(vc.rgbD[0], 1.021, 5e-4);
  EXPECT_NEAR(vc.rgbD[1], 0.986, 5e-4);
  EXPECT_NEAR(vc.rgbD[2], 0.934, 5e-4);
  EXPECT_NEAR(vc.fl, 0.388, 5e-4);
  EXPECT_NEAR(vc.flRoot, 0.789, 5e-4);
  EXPECT_NEAR(vc.z, 1.909, 5e-4);
}

// ---- HCT ----------------------------------------------------------------------------------

TEST(ColorSchemeHct, FromArgbMatchesReferenceValues) {
  const Hct green(kGreen);
  EXPECT_NEAR(green.hue(), 142.139, 5e-3);
  EXPECT_NEAR(green.chroma(), 108.410, 5e-3);
  EXPECT_NEAR(green.tone(), 87.737, 5e-3);

  const Hct blue(kBlue);
  EXPECT_NEAR(blue.hue(), 282.788, 5e-3);
  EXPECT_NEAR(blue.chroma(), 87.230, 5e-3);
  EXPECT_NEAR(blue.tone(), 32.302, 5e-3);
}

TEST(ColorSchemeHct, BlueAtTone90LosesChromaToTheGamut) {
  const Hct hct(282.788, 87.230, 90.0);
  EXPECT_NEAR(hct.hue(), 282.239, 5e-3);
  EXPECT_NEAR(hct.chroma(), 19.144, 5e-3);
  EXPECT_NEAR(hct.tone(), 90.035, 5e-3);
}

TEST(ColorSchemeHct, RoundTripPreservesOriginalColor) {
  // 512 of the 16.7M colours, as the reference's round-trip test.
  for (int r = 0; r < 296; r += 37) {
    for (int g = 0; g < 296; g += 37) {
      for (int b = 0; b < 296; b += 37) {
        const Argb argb = argbFromRgb(std::min(255, r), std::min(255, g), std::min(255, b));
        const Hct hct(argb);
        EXPECT_EQ(Hct(hct.hue(), hct.chroma(), hct.tone()).argb(), argb) << hex(argb).toStdString();
      }
    }
  }
}

TEST(ColorSchemeHct, SolverReturnsASufficientlyCloseColor) {
  auto onBoundary = [](Argb argb) {
    return redOf(argb) == 0 || redOf(argb) == 255 || greenOf(argb) == 0 || greenOf(argb) == 255 ||
           blueOf(argb) == 0 || blueOf(argb) == 255;
  };
  for (int hue = 15; hue < 360; hue += 30) {
    for (int chroma = 0; chroma <= 100; chroma += 10) {
      for (int tone = 20; tone <= 80; tone += 10) {
        const Hct hct(hue, chroma, tone);
        SCOPED_TRACE(testing::Message()
                     << "hue " << hue << " chroma " << chroma << " tone " << tone);
        if (chroma > 0) EXPECT_LE(diffDegrees(hct.hue(), hue), 4.0);
        EXPECT_GE(hct.chroma(), 0.0);
        EXPECT_LE(hct.chroma(), chroma + 2.5);
        // Short of the chroma means the colour sits on the gamut boundary.
        if (hct.chroma() < chroma - 2.5) EXPECT_TRUE(onBoundary(hct.argb()));
        EXPECT_LE(std::fabs(hct.tone() - tone), 0.5);
      }
    }
  }
}

TEST(ColorSchemeHct, SettersKeepTheOtherTwoCoordinates) {
  Hct hct(kBaselineSeed);
  hct.setTone(80.0);
  EXPECT_NEAR(hct.tone(), 80.0, 0.5);
  hct.setHue(120.0);
  EXPECT_NEAR(diffDegrees(hct.hue(), 120.0), 0.0, 4.0);
  hct.setChroma(0.0);
  EXPECT_NEAR(hct.chroma(), 0.0, 3.0);
}

// ---- Tonal palettes -----------------------------------------------------------------------

TEST(ColorSchemeTonalPalette, ToneSweepOfBlue) {
  const TonalPalette blue{Hct(kBlue)};
  const struct {
    double tone;
    Argb argb;
  } sweep[] = {{100, 0xffffffff}, {95, 0xfff1efff}, {90, 0xffe0e0ff}, {80, 0xffbec2ff},
               {70, 0xff9da3ff},  {60, 0xff7c84ff}, {50, 0xff5a64ff}, {40, 0xff343dff},
               {30, 0xff0000ef},  {20, 0xff0001ac}, {10, 0xff00006e}, {0, 0xff000000}};
  for (const auto& step : sweep) {
    EXPECT_EQ(blue.tone(step.tone), step.argb) << "tone " << step.tone;
  }
}

TEST(ColorSchemeTonalPalette, CorePaletteOfBlue) {
  // a1 and a2 of CorePalette.of(blue): chroma max(48, c) and 16 at the seed hue.
  const Hct seed(kBlue);
  const TonalPalette a1(seed.hue(), std::max(48.0, seed.chroma()));
  EXPECT_EQ(a1.tone(90), 0xffe0e0ffu);
  EXPECT_EQ(a1.tone(40), 0xff343dffu);
  const TonalPalette a2(seed.hue(), 16.0);
  const struct {
    double tone;
    Argb argb;
  } sweep[] = {{95, 0xfff1efff}, {90, 0xffe1e0f9}, {80, 0xffc5c4dd}, {70, 0xffa9a9c1},
               {60, 0xff8f8fa6}, {50, 0xff75758b}, {40, 0xff5c5d72}, {30, 0xff444559},
               {20, 0xff2e2f42}, {10, 0xff191a2c}};
  for (const auto& step : sweep) {
    EXPECT_EQ(a2.tone(step.tone), step.argb) << "tone " << step.tone;
  }
}

TEST(ColorSchemeTonalPalette, KeyColors) {
  {
    const Hct key = TonalPalette(50.0, 60.0).keyColor();  // exactly achievable
    EXPECT_NEAR(key.hue(), 50.0, 10.0);
    EXPECT_NEAR(key.chroma(), 60.0, 0.5);
    EXPECT_GT(key.tone(), 0);
    EXPECT_LT(key.tone(), 100);
  }
  {
    // Above what the gamut offers: hue 149 peaks at chroma 89.6 near tone 87.9.
    const Hct key = TonalPalette(149.0, 200.0).keyColor();
    EXPECT_NEAR(key.hue(), 149.0, 10.0);
    EXPECT_GT(key.chroma(), 89.0);
  }
  {
    // Very low chroma is available almost everywhere, so the key color stays at tone 50.
    const Hct key = TonalPalette(50.0, 3.0).keyColor();
    EXPECT_NEAR(key.chroma(), 3.0, 0.5);
    EXPECT_NEAR(key.tone(), 50.0, 0.5);
  }
}

// ---- Schemes ------------------------------------------------------------------------------

// The Material 3 baseline (m3.material.io, "Baseline color scheme") is the CorePalette of the seed
// #6750A4: primary chroma 48, secondary 16, tertiary 24 at +60 degrees, neutral 4, neutral variant
// 8. The published role values are tones of those palettes. The website values were generated by an
// older HCT solver, so some of them differ from this one by one step in one channel; those are
// compared per channel with a tolerance of 1. The error colours are not in the check: the website's
// #B3261E is hand-picked, not a tone of the error palette.
class ColorSchemeBaseline : public testing::Test {
 protected:
  void SetUp() override {
    const Hct seed(kBaselineSeed);
    primary_ = TonalPalette(seed.hue(), std::max(48.0, seed.chroma()));
    secondary_ = TonalPalette(seed.hue(), 16.0);
    tertiary_ = TonalPalette(sanitizeDegrees(seed.hue() + 60.0), 24.0);
    neutral_ = TonalPalette(seed.hue(), 4.0);
    neutralVariant_ = TonalPalette(seed.hue(), 8.0);
  }

  static void expectNear(Argb actual, unsigned expectedRgb, const char* what) {
    const int er = static_cast<int>((expectedRgb >> 16) & 0xff);
    const int eg = static_cast<int>((expectedRgb >> 8) & 0xff);
    const int eb = static_cast<int>(expectedRgb & 0xff);
    EXPECT_LE(std::abs(redOf(actual) - er), 1) << what << " " << hex(actual).toStdString();
    EXPECT_LE(std::abs(greenOf(actual) - eg), 1) << what << " " << hex(actual).toStdString();
    EXPECT_LE(std::abs(blueOf(actual) - eb), 1) << what << " " << hex(actual).toStdString();
  }

  TonalPalette primary_;
  TonalPalette secondary_;
  TonalPalette tertiary_;
  TonalPalette neutral_;
  TonalPalette neutralVariant_;
};

TEST_F(ColorSchemeBaseline, LightRoles) {
  // Exact: the roles every other check hangs on.
  EXPECT_COLOR(primary_.tone(40), "#6750A4");    // primary
  EXPECT_COLOR(primary_.tone(100), "#FFFFFF");   // onPrimary
  EXPECT_COLOR(secondary_.tone(40), "#625B71");  // secondary
  EXPECT_COLOR(secondary_.tone(90), "#E8DEF8");  // secondaryContainer
  expectNear(primary_.tone(90), 0xEADDFF, "primaryContainer");
  expectNear(primary_.tone(10), 0x21005D, "onPrimaryContainer");
  expectNear(secondary_.tone(10), 0x1D192B, "onSecondaryContainer");
  expectNear(tertiary_.tone(40), 0x7D5260, "tertiary");
  expectNear(tertiary_.tone(90), 0xFFD8E4, "tertiaryContainer");
  expectNear(tertiary_.tone(10), 0x31111D, "onTertiaryContainer");
  expectNear(neutral_.tone(99), 0xFFFBFE, "background, surface");
  expectNear(neutral_.tone(10), 0x1C1B1F, "onSurface");
  expectNear(neutralVariant_.tone(90), 0xE7E0EC, "surfaceVariant");
  expectNear(neutralVariant_.tone(30), 0x49454F, "onSurfaceVariant");
  expectNear(neutralVariant_.tone(50), 0x79747E, "outline");
  expectNear(neutralVariant_.tone(80), 0xCAC4D0, "outlineVariant");
}

TEST_F(ColorSchemeBaseline, DarkRoles) {
  EXPECT_COLOR(primary_.tone(20), "#381E72");   // onPrimary
  EXPECT_COLOR(tertiary_.tone(80), "#EFB8C8");  // tertiary
  expectNear(primary_.tone(80), 0xD0BCFF, "primary");
  expectNear(primary_.tone(30), 0x4F378B, "primaryContainer");
  expectNear(primary_.tone(90), 0xEADDFF, "onPrimaryContainer");
  expectNear(secondary_.tone(80), 0xCCC2DC, "secondary");
  expectNear(secondary_.tone(30), 0x4A4458, "secondaryContainer");
  expectNear(tertiary_.tone(30), 0x633B48, "tertiaryContainer");
  expectNear(neutral_.tone(10), 0x1C1B1F, "background, surface");
  expectNear(neutral_.tone(90), 0xE6E1E5, "onSurface");
  expectNear(neutralVariant_.tone(30), 0x49454F, "surfaceVariant");
  expectNear(neutralVariant_.tone(80), 0xCAC4D0, "onSurfaceVariant");
  expectNear(neutralVariant_.tone(60), 0x938F99, "outline");
}

// The tones of the 2021 colour spec at the standard contrast level, for TonalSpot. They come from
// the reference's role table (cpp/dynamiccolor/material_dynamic_colors.cc) and are what the
// contrast logic must leave alone at contrast 0 on this seed. A colour's own tone matches within
// the rounding of 8-bit sRGB.
TEST(ColorSchemeDynamic, TonalSpotRoleTonesFollowTheSpec) {
  struct Expected {
    Role role;
    double light;
    double dark;
  };
  const Expected expected[] = {
      {Role::Background, 98, 6},
      {Role::Surface, 98, 6},
      {Role::SurfaceDim, 87, 6},
      {Role::SurfaceBright, 98, 24},
      {Role::SurfaceContainerLowest, 100, 4},
      {Role::SurfaceContainerLow, 96, 10},
      {Role::SurfaceContainer, 94, 12},
      {Role::SurfaceContainerHigh, 92, 17},
      {Role::SurfaceContainerHighest, 90, 22},
      {Role::OnSurface, 10, 90},
      {Role::SurfaceVariant, 90, 30},
      {Role::OnSurfaceVariant, 30, 80},
      {Role::InverseSurface, 20, 90},
      {Role::InverseOnSurface, 95, 20},
      {Role::Outline, 50, 60},
      {Role::OutlineVariant, 80, 30},
      {Role::Shadow, 0, 0},
      {Role::Scrim, 0, 0},
      {Role::Primary, 40, 80},
      {Role::OnPrimary, 100, 20},
      {Role::PrimaryContainer, 90, 30},
      {Role::OnPrimaryContainer, 30, 90},
      {Role::InversePrimary, 80, 40},
      {Role::Secondary, 40, 80},
      {Role::OnSecondary, 100, 20},
      {Role::SecondaryContainer, 90, 30},
      {Role::OnSecondaryContainer, 30, 90},
      {Role::Tertiary, 40, 80},
      {Role::OnTertiary, 100, 20},
      {Role::TertiaryContainer, 90, 30},
      {Role::OnTertiaryContainer, 30, 90},
      {Role::Error, 40, 80},
      {Role::OnError, 100, 20},
      {Role::ErrorContainer, 90, 30},
      {Role::OnErrorContainer, 30, 90},
      {Role::PrimaryFixed, 90, 90},
      {Role::PrimaryFixedDim, 80, 80},
      {Role::OnPrimaryFixed, 10, 10},
      {Role::OnPrimaryFixedVariant, 30, 30},
      {Role::SecondaryFixed, 90, 90},
      {Role::SecondaryFixedDim, 80, 80},
      {Role::TertiaryFixed, 90, 90},
      {Role::TertiaryFixedDim, 80, 80},
  };
  const DynamicScheme light = makeScheme(kBaselineSeed, Variant::TonalSpot, false);
  const DynamicScheme dark = makeScheme(kBaselineSeed, Variant::TonalSpot, true);
  for (const Expected& e : expected) {
    SCOPED_TRACE(std::string(roleName(e.role)));
    EXPECT_NEAR(roleTone(e.role, light), e.light, 0.01);
    EXPECT_NEAR(roleTone(e.role, dark), e.dark, 0.01);
    EXPECT_NEAR(Hct(roleColor(e.role, light)).tone(), e.light, 0.8);
    EXPECT_NEAR(Hct(roleColor(e.role, dark)).tone(), e.dark, 0.8);
  }
}

// Colours that two independent sources agree on: this port and Flutter's
// ColorScheme.fromSeed(Color(0xFF6750A4)) / the M3 baseline. TonalSpot uses chroma 36 for primary,
// so its primary is the muted #65558F, not the baseline #6750A4.
TEST(ColorSchemeDynamic, TonalSpotOfTheBaselineSeed) {
  const DynamicScheme light = makeScheme(kBaselineSeed, Variant::TonalSpot, false);
  EXPECT_COLOR(roleColor(Role::Primary, light), "#65558F");
  EXPECT_COLOR(roleColor(Role::OnPrimary, light), "#FFFFFF");
  EXPECT_COLOR(roleColor(Role::PrimaryContainer, light), "#E9DDFF");
  EXPECT_COLOR(roleColor(Role::Secondary, light), "#625B71");
  EXPECT_COLOR(roleColor(Role::SecondaryContainer, light), "#E8DEF8");
  EXPECT_COLOR(roleColor(Role::OnSurface, light), "#1D1B20");
  EXPECT_COLOR(roleColor(Role::Error, light), "#BA1A1A");
  EXPECT_COLOR(roleColor(Role::ErrorContainer, light), "#FFDAD6");

  const DynamicScheme dark = makeScheme(kBaselineSeed, Variant::TonalSpot, true);
  EXPECT_COLOR(roleColor(Role::OnPrimary, dark), "#36275D");
  EXPECT_COLOR(roleColor(Role::OnPrimaryContainer, dark), "#E9DDFF");
  EXPECT_COLOR(roleColor(Role::Tertiary, dark), "#EFB8C8");
  EXPECT_COLOR(roleColor(Role::Surface, dark), "#141218");
  EXPECT_COLOR(roleColor(Role::OnSurface, dark), "#E6E0E9");
  EXPECT_COLOR(roleColor(Role::Error, dark), "#FFB4AB");
}

TEST(ColorSchemeDynamic, MonochromeTonesMatchTheReferenceTests) {
  const struct {
    Role role;
    double dark;
    double light;
  } expected[] = {
      {Role::Primary, 100, 0},
      {Role::OnPrimary, 10, 90},
      {Role::PrimaryContainer, 85, 25},
      {Role::OnPrimaryContainer, 0, 100},
      {Role::Secondary, 80, 40},
      {Role::OnSecondary, 10, 100},
      {Role::SecondaryContainer, 30, 85},
      {Role::OnSecondaryContainer, 90, 10},
      {Role::Tertiary, 90, 25},
      {Role::OnTertiary, 10, 90},
      {Role::TertiaryContainer, 60, 49},
      {Role::OnTertiaryContainer, 0, 100},
  };
  const DynamicScheme dark = makeScheme(kBlue, Variant::Monochrome, true);
  const DynamicScheme light = makeScheme(kBlue, Variant::Monochrome, false);
  for (const auto& e : expected) {
    SCOPED_TRACE(std::string(roleName(e.role)));
    EXPECT_NEAR(Hct(roleColor(e.role, dark)).tone(), e.dark, 1.0);
    EXPECT_NEAR(Hct(roleColor(e.role, light)).tone(), e.light, 1.0);
  }
}

TEST(ColorSchemeDynamic, EveryVariantProducesAFullReadableScheme) {
  for (const Argb seed : {kBaselineSeed, kRed, 0xff1e64c8u, 0xff808080u, kBlack, kWhite}) {
    for (const Variant variant : allVariants()) {
      for (const bool dark : {false, true}) {
        for (const double contrast : {-1.0, 0.0, 0.5, 1.0}) {
          SCOPED_TRACE(testing::Message()
                       << hex(seed).toStdString() << " " << std::string(variantName(variant))
                       << " dark " << dark << " contrast " << contrast);
          const DynamicScheme s = makeScheme(seed, variant, dark, contrast);
          for (int i = 0; i < static_cast<int>(Role::Count_); ++i) {
            const Role role = static_cast<Role>(i);
            EXPECT_FALSE(roleName(role).empty()) << i;
            EXPECT_EQ(alphaOf(roleColor(role, s)), 255) << roleName(role);
          }
          // Body text keeps usable contrast on its surface at the standard level and above.
          if (contrast >= 0.0) {
            const double ratio = ratioOfTones(Hct(roleColor(Role::OnSurface, s)).tone(),
                                              Hct(roleColor(Role::Surface, s)).tone());
            EXPECT_GE(ratio, 7.0 - 0.2);
          }
          if (variant != Variant::Monochrome && contrast >= 0.0) {
            EXPECT_GE(ratioOfTones(Hct(roleColor(Role::OnPrimary, s)).tone(),
                                   Hct(roleColor(Role::Primary, s)).tone()),
                      4.5 - 0.2);
          }
        }
      }
    }
  }
}

TEST(ColorSchemeDynamic, HigherContrastWidensTextAgainstSurface) {
  const DynamicScheme standard = makeScheme(kBaselineSeed, Variant::TonalSpot, false, 0.0);
  const DynamicScheme high = makeScheme(kBaselineSeed, Variant::TonalSpot, false, 1.0);
  auto ratio = [](const DynamicScheme& s) {
    return ratioOfTones(Hct(roleColor(Role::OnSurfaceVariant, s)).tone(),
                        Hct(roleColor(Role::Surface, s)).tone());
  };
  EXPECT_GT(ratio(high), ratio(standard));
}

TEST(ColorSchemeDynamic, VariantsDifferAsDesigned) {
  const DynamicScheme tonal = makeScheme(kBaselineSeed, Variant::TonalSpot, true);
  const DynamicScheme vibrant = makeScheme(kBaselineSeed, Variant::Vibrant, true);
  const DynamicScheme neutral = makeScheme(kBaselineSeed, Variant::Neutral, true);
  const DynamicScheme mono = makeScheme(kBaselineSeed, Variant::Monochrome, true);
  const DynamicScheme fidelity = makeScheme(kBaselineSeed, Variant::Fidelity, false);
  EXPECT_GT(Hct(roleColor(Role::Primary, vibrant)).chroma(),
            Hct(roleColor(Role::Primary, tonal)).chroma());
  EXPECT_LT(Hct(roleColor(Role::Primary, neutral)).chroma(),
            Hct(roleColor(Role::Primary, tonal)).chroma());
  {
    // Chroma 0 gives a grey (pure white has CAM16 chroma 2.9, so test the channels, not chroma).
    const Argb primary = roleColor(Role::Primary, mono);
    EXPECT_EQ(redOf(primary), greenOf(primary));
    EXPECT_EQ(greenOf(primary), blueOf(primary));
  }
  // Fidelity keeps the source colour as the primary container.
  EXPECT_NEAR(Hct(roleColor(Role::PrimaryContainer, fidelity)).tone(), Hct(kBaselineSeed).tone(),
              1.5);
}

TEST(ColorSchemeDynamic, VariantNamesParse) {
  EXPECT_EQ(parseVariant("TonalSpot"), Variant::TonalSpot);
  EXPECT_EQ(parseVariant("tonal_spot"), Variant::TonalSpot);
  EXPECT_EQ(parseVariant("fruit-salad"), Variant::FruitSalad);
  EXPECT_EQ(parseVariant("MONOCHROME"), Variant::Monochrome);
  EXPECT_FALSE(parseVariant("nope").has_value());
  for (const Variant variant : allVariants())
    EXPECT_EQ(parseVariant(variantName(variant)), variant);
  EXPECT_EQ(allVariants().size(), 9u);
}

TEST(ColorSchemeDynamic, TemperatureCacheFindsAComplement) {
  // The complement of a warm red is on the cool side, and analogous colours stay close in hue.
  const Hct red(kRed);
  const TemperatureCache cache(red);
  const Hct complement = cache.complement();
  EXPECT_GT(diffDegrees(red.hue(), complement.hue()), 90.0);
  const std::vector<Hct> analogous = cache.analogousColors(5, 12);
  ASSERT_EQ(analogous.size(), 5u);
  EXPECT_EQ(analogous[2].argb(), red.argb());
}

// ---- Quantizer and Score ------------------------------------------------------------------

TEST(ColorSchemeQuantizer, CelebiOnPureColors) {
  {
    const ColorCounts one = quantizeCelebi({kRed}, 128);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one.at(kRed), 1u);
  }
  {
    const ColorCounts five = quantizeCelebi({kBlue, kBlue, kBlue, kBlue, kBlue}, 128);
    ASSERT_EQ(five.size(), 1u);
    EXPECT_EQ(five.at(kBlue), 5u);
  }
  {
    const ColorCounts two = quantizeCelebi({kRed, kRed, kGreen, kGreen, kGreen}, 128);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_EQ(two.at(kRed), 2u);
    EXPECT_EQ(two.at(kGreen), 3u);
  }
  {
    const ColorCounts three = quantizeCelebi({kRed, kGreen, kBlue}, 128);
    ASSERT_EQ(three.size(), 3u);
    EXPECT_EQ(three.at(kRed), 1u);
    EXPECT_EQ(three.at(kGreen), 1u);
    EXPECT_EQ(three.at(kBlue), 1u);
  }
  EXPECT_TRUE(quantizeCelebi({}, 128).empty());
}

TEST(ColorSchemeQuantizer, CelebiIgnoresTransparentPixels) {
  const ColorCounts counts = quantizeCelebi({kRed, 0x00112233u, kRed}, 16);
  ASSERT_EQ(counts.size(), 1u);
  EXPECT_EQ(counts.at(kRed), 2u);
}

TEST(ColorSchemeQuantizer, WuReducesToTheRequestedCount) {
  std::vector<Argb> pixels;
  for (int i = 0; i < 64; ++i) pixels.push_back(argbFromRgb(i * 4, 255 - i * 4, (i * 37) % 256));
  const std::vector<Argb> colors = quantizeWu(pixels, 4);
  EXPECT_GE(colors.size(), 1u);
  EXPECT_LE(colors.size(), 4u);
}

TEST(ColorSchemeScore, PrioritizesChroma) {
  const ColorCounts colors{{kBlack, 1}, {kWhite, 1}, {kBlue, 1}};
  const std::vector<Argb> ranked = rankedSuggestions(colors, {.desired = 4});
  ASSERT_EQ(ranked.size(), 1u);
  EXPECT_EQ(ranked[0], kBlue);
}

TEST(ColorSchemeScore, PrioritizesChromaWhenProportionsAreEqual) {
  const ColorCounts colors{{kRed, 1}, {kGreen, 1}, {kBlue, 1}};
  const std::vector<Argb> ranked = rankedSuggestions(colors, {.desired = 4});
  ASSERT_EQ(ranked.size(), 3u);
  EXPECT_EQ(ranked[0], kRed);
  EXPECT_EQ(ranked[1], kGreen);
  EXPECT_EQ(ranked[2], kBlue);
}

TEST(ColorSchemeScore, FallsBackToGoogleBlue) {
  const std::vector<Argb> ranked = rankedSuggestions({{kBlack, 1}}, {.desired = 4});
  ASSERT_EQ(ranked.size(), 1u);
  EXPECT_EQ(ranked[0], 0xff4285f4u);
}

TEST(ColorSchemeScore, DedupesNearbyHues) {
  const ColorCounts colors{{0xff008772, 1}, {0xff318477, 1}};
  const std::vector<Argb> ranked = rankedSuggestions(colors, {.desired = 4});
  ASSERT_EQ(ranked.size(), 1u);
  EXPECT_EQ(ranked[0], 0xff008772u);
}

TEST(ColorSchemeScore, MaximizesHueDistance) {
  const ColorCounts colors{{0xff008772, 1}, {0xff008587, 1}, {0xff007ebc, 1}};
  const std::vector<Argb> ranked = rankedSuggestions(colors, {.desired = 2});
  ASSERT_EQ(ranked.size(), 2u);
  EXPECT_EQ(ranked[0], 0xff007ebcu);
  EXPECT_EQ(ranked[1], 0xff008772u);
}

TEST(ColorSchemeScore, GeneratedScenarios) {
  {
    const ColorCounts colors{{0xff7ea16d, 67}, {0xffd8ccae, 67}, {0xff835c0d, 49}};
    const auto ranked =
        rankedSuggestions(colors, {.desired = 3, .fallback = 0xff8d3819, .filter = false});
    EXPECT_EQ(ranked, (std::vector<Argb>{0xff7ea16d, 0xffd8ccae, 0xff835c0d}));
  }
  {
    const ColorCounts colors{
        {0xffd33881, 14}, {0xff3205cc, 77}, {0xff0b48cf, 36}, {0xffa08f5d, 81}};
    const auto ranked =
        rankedSuggestions(colors, {.desired = 4, .fallback = 0xff7d772b, .filter = true});
    EXPECT_EQ(ranked, (std::vector<Argb>{0xff3205cc, 0xffa08f5d, 0xffd33881}));
  }
  {
    const ColorCounts colors{
        {0xffbe94a6, 23}, {0xffc33fd7, 42}, {0xff899f36, 90}, {0xff94c574, 82}};
    const auto ranked =
        rankedSuggestions(colors, {.desired = 3, .fallback = 0xffaa79a4, .filter = true});
    EXPECT_EQ(ranked, (std::vector<Argb>{0xff94c574, 0xffc33fd7, 0xffbe94a6}));
  }
  {
    const ColorCounts colors{
        {0xffdf241c, 85}, {0xff685859, 44}, {0xffd06d5f, 34}, {0xff561c54, 27}, {0xff713090, 88}};
    const auto ranked =
        rankedSuggestions(colors, {.desired = 5, .fallback = 0xff58c19c, .filter = false});
    EXPECT_EQ(ranked, (std::vector<Argb>{0xffdf241c, 0xff561c54}));
  }
  {
    const ColorCounts colors{
        {0xff18ea8f, 93}, {0xff327593, 18}, {0xff066a18, 53}, {0xfffa8a23, 74}, {0xff04ca1f, 62}};
    const auto ranked =
        rankedSuggestions(colors, {.desired = 2, .fallback = 0xff4c377a, .filter = false});
    EXPECT_EQ(ranked, (std::vector<Argb>{0xff18ea8f, 0xfffa8a23}));
  }
}

// ---- Image seed ---------------------------------------------------------------------------

QImage solidImage(int w, int h, QRgb color) {
  QImage image(w, h, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}

TEST(ColorSchemeImage, DominantColorBecomesTheSeed) {
  // 80% of the pixels are #1E64C8, the rest white.
  QImage image = solidImage(100, 100, qRgb(255, 255, 255));
  QPainter painter(&image);
  painter.fillRect(0, 0, 100, 80, QColor(0x1e, 0x64, 0xc8));
  painter.end();
  const std::optional<Argb> seed = seedColorFromImage(image);
  ASSERT_TRUE(seed.has_value());
  const Hct got(*seed);
  const Hct want(0xff1e64c8);
  EXPECT_NEAR(diffDegrees(got.hue(), want.hue()), 0.0, 1.0);
  EXPECT_NEAR(got.chroma(), want.chroma(), 2.0);
  EXPECT_NEAR(got.tone(), want.tone(), 1.0);
  EXPECT_NEAR(redOf(*seed), 0x1e, 2);
  EXPECT_NEAR(greenOf(*seed), 0x64, 2);
  EXPECT_NEAR(blueOf(*seed), 0xc8, 2);
}

TEST(ColorSchemeImage, MoreFrequentColorfulHueWinsOverARarerOne) {
  // 85% teal, 15% orange: the teal is the dominant colour.
  QImage image = solidImage(100, 100, qRgb(0x00, 0x96, 0x88));
  QPainter painter(&image);
  painter.fillRect(0, 85, 100, 15, QColor(0xff, 0x98, 0x00));
  painter.end();
  const std::optional<Argb> seed = seedColorFromImage(image);
  ASSERT_TRUE(seed.has_value());
  EXPECT_NEAR(diffDegrees(Hct(*seed).hue(), Hct(0xff009688).hue()), 0.0, 3.0);
}

TEST(ColorSchemeImage, GreyImageFallsBackToGoogleBlue) {
  const std::optional<Argb> seed = seedColorFromImage(solidImage(32, 32, qRgb(120, 120, 120)));
  ASSERT_TRUE(seed.has_value());
  EXPECT_EQ(*seed, 0xff4285f4u);
  EXPECT_FALSE(seedColorFromImage(QImage()).has_value());
}

TEST(ColorSchemeImage, MeanLuminance) {
  EXPECT_NEAR(meanLuminance(solidImage(8, 8, qRgb(255, 255, 255))), 1.0, 1e-9);
  EXPECT_NEAR(meanLuminance(solidImage(8, 8, qRgb(0, 0, 0))), 0.0, 1e-9);
  EXPECT_NEAR(meanLuminance(solidImage(8, 8, qRgb(128, 128, 128))), 128.0 / 255.0, 1e-6);
  // Half white, half black.
  QImage half = solidImage(10, 10, qRgb(0, 0, 0));
  QPainter painter(&half);
  painter.fillRect(0, 0, 5, 10, Qt::white);
  painter.end();
  EXPECT_NEAR(meanLuminance(half), 0.5, 1e-9);
  // Rec. 709 weights: pure green is brighter than pure blue.
  EXPECT_GT(meanLuminance(solidImage(4, 4, qRgb(0, 255, 0))),
            meanLuminance(solidImage(4, 4, qRgb(0, 0, 255))));
  EXPECT_EQ(meanLuminance(QImage()), 0.0);
}

// ---- The QML service ----------------------------------------------------------------------

TEST(ColorSchemeService, FromColorGivesPlainAndM3AliasedRoles) {
  ColorSchemeService service;
  const SchemeValue scheme =
      service.fromColor(QStringLiteral("#6750A4"), QStringLiteral("tonalspot"), true);
  ASSERT_TRUE(scheme.valid);
  EXPECT_EQ(scheme.variant, QStringLiteral("tonalspot"));
  EXPECT_TRUE(scheme.dark);
  EXPECT_EQ(scheme.colors.size(), static_cast<int>(Role::Count_));
  const Argb primary =
      roleColor(Role::Primary, makeScheme(kBaselineSeed, Variant::TonalSpot, true));
  const QColor expected(redOf(primary), greenOf(primary), blueOf(primary));
  EXPECT_EQ(scheme.colors.value(QStringLiteral("primary")).value<QColor>(), expected);
  EXPECT_TRUE(scheme.colors.contains(QStringLiteral("onSurface")));
  EXPECT_TRUE(scheme.colors.contains(QStringLiteral("surfaceContainerHighest")));
  EXPECT_TRUE(scheme.colors.contains(QStringLiteral("primary_paletteKeyColor")));
  EXPECT_TRUE(scheme.colors.contains(QStringLiteral("onPrimaryFixedVariant")));
  EXPECT_EQ(scheme.source, QColor("#6750A4"));
}

TEST(ColorSchemeService, EmptyVariantIsTonalSpotAndBadInputIsInvalid) {
  ColorSchemeService service;
  EXPECT_EQ(service.fromColor(QColor("#6750A4")).variant, QStringLiteral("tonalspot"));
  EXPECT_FALSE(service.fromColor(QStringLiteral("#6750A4"), QStringLiteral("nope")).valid);
  EXPECT_FALSE(service.fromColor(QStringLiteral("not a colour")).valid);
  EXPECT_EQ(service.variants().size(), 9);
  EXPECT_TRUE(service.variants().contains(QStringLiteral("fruitsalad")));
}

TEST(ColorSchemeService, FromImageIsAsynchronous) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("wall.png"));
  ASSERT_TRUE(solidImage(64, 64, qRgb(0x1e, 0x64, 0xc8)).save(path));

  ColorSchemeService service;
  QSignalSpy finished(&service, &ColorSchemeService::fromImageFinished);
  QSignalSpy failed(&service, &ColorSchemeService::fromImageFailed);
  service.fromImage(path, QStringLiteral("vibrant"), false, 0.5);
  EXPECT_EQ(finished.count(), 0);  // nothing yet: the work runs on a pool thread
  ASSERT_TRUE(finished.wait(10000));
  EXPECT_EQ(failed.count(), 0);
  const SchemeValue scheme = finished.at(0).at(1).value<SchemeValue>();
  EXPECT_EQ(finished.at(0).at(0).toString(), path);
  ASSERT_TRUE(scheme.valid);
  EXPECT_EQ(scheme.variant, QStringLiteral("vibrant"));
  EXPECT_FALSE(scheme.dark);
  EXPECT_DOUBLE_EQ(scheme.contrast, 0.5);
  EXPECT_NEAR(scheme.source.red(), 0x1e, 2);
  EXPECT_NEAR(scheme.source.blue(), 0xc8, 2);
}

TEST(ColorSchemeService, FromImageReportsMissingFilesAndBadVariants) {
  ColorSchemeService service;
  QSignalSpy failed(&service, &ColorSchemeService::fromImageFailed);
  service.fromImage(QStringLiteral("D:/definitely/not/here.png"));
  ASSERT_TRUE(failed.wait(10000));
  EXPECT_FALSE(failed.at(0).at(1).toString().isEmpty());
  service.fromImage(QStringLiteral("x.png"), QStringLiteral("nope"));
  EXPECT_EQ(failed.count(), 2);  // a bad variant fails at once
}

TEST(ColorSchemeService, GenerateTakesAColourOrAnImageFile) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("wall.png"));
  ASSERT_TRUE(solidImage(16, 16, qRgb(0xc8, 0x1e, 0x64)).save(path));

  ColorSchemeService service;
  EXPECT_TRUE(service.generate(QStringLiteral("#336699"), QStringLiteral("neutral"), true).valid);
  EXPECT_TRUE(service.generate(QColor("#336699")).valid);
  QSignalSpy finished(&service, &ColorSchemeService::fromImageFinished);
  EXPECT_FALSE(service.generate(path, QStringLiteral("content"), true).valid);  // async: later
  ASSERT_TRUE(finished.wait(10000));
  EXPECT_EQ(finished.at(0).at(1).value<SchemeValue>().variant, QStringLiteral("content"));
  QSignalSpy failed(&service, &ColorSchemeService::fromImageFailed);
  EXPECT_FALSE(service.generate(QStringLiteral("neither colour nor file")).valid);
  EXPECT_EQ(failed.count(), 1);
}

TEST(ColorSchemeService, LuminanceOfAnImageFile) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString white = dir.filePath(QStringLiteral("white.png"));
  const QString black = dir.filePath(QStringLiteral("black.png"));
  ASSERT_TRUE(solidImage(300, 200, qRgb(255, 255, 255)).save(white));
  ASSERT_TRUE(solidImage(10, 10, qRgb(0, 0, 0)).save(black));
  ColorSchemeService service;
  EXPECT_NEAR(service.luminance(white), 1.0, 1e-9);
  EXPECT_NEAR(service.luminance(QUrl::fromLocalFile(black).toString()), 0.0, 1e-9);
  EXPECT_EQ(service.luminance(dir.filePath(QStringLiteral("missing.png"))), -1.0);
  EXPECT_TRUE(service.seedColor(white).isValid());
}

TEST(ColorSchemeService, IsUsableFromQml) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import wabar.services
QtObject {
    property var scheme: ColorScheme.fromColor("#6750A4", "tonalspot", false)
    property var primary: scheme.colors.primary
    property var onSurface: ColorScheme.fromColor("#6750A4", "tonalspot", true).colors.onSurface
    property bool valid: scheme.valid
    property string variant: scheme.variant
    property int variantCount: ColorScheme.variants.length
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_TRUE(root->property("valid").toBool());
  EXPECT_EQ(root->property("variant").toString(), QStringLiteral("tonalspot"));
  EXPECT_EQ(root->property("primary").value<QColor>(), QColor("#65558F"));
  EXPECT_EQ(root->property("onSurface").value<QColor>(), QColor("#E6E0E9"));
  EXPECT_EQ(root->property("variantCount").toInt(), 9);
}

}  // namespace

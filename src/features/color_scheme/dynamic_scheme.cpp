#include "features/color_scheme/dynamic_scheme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>

namespace waku::color_scheme {

namespace {

// ---- Variants -----------------------------------------------------------------------------

struct VariantName {
  Variant variant;
  std::string_view name;
};

constexpr VariantName kVariantNames[] = {
    {Variant::TonalSpot, "tonalspot"},   {Variant::Vibrant, "vibrant"},
    {Variant::Expressive, "expressive"}, {Variant::Neutral, "neutral"},
    {Variant::Monochrome, "monochrome"}, {Variant::Fidelity, "fidelity"},
    {Variant::Content, "content"},       {Variant::Rainbow, "rainbow"},
    {Variant::FruitSalad, "fruitsalad"},
};

// The hue of the source rotated by the entry whose breakpoint range holds it (breakpoint[i] <= hue
// < breakpoint[i + 1]); the source hue itself when no range holds it.
double rotatedHue(const Hct& source, const std::vector<double>& breakpoints,
                  const std::vector<double>& rotations) {
  const size_t size = std::min(breakpoints.size() - 1, rotations.size());
  const double hue = source.hue();
  for (size_t i = 0; i < size; ++i) {
    if (hue >= breakpoints[i] && hue < breakpoints[i + 1]) {
      return sanitizeDegrees(hue + rotations[i]);
    }
  }
  return hue;
}

// Android's "disliked" colours: dark yellow-greens. They are lightened to tone 70.
Hct fixIfDisliked(const Hct& hct) {
  const double hue = std::round(hct.hue());
  const bool disliked = hue >= 90.0 && hue <= 111.0 && std::round(hct.chroma()) > 16.0 &&
                        std::round(hct.tone()) < 65.0;
  return disliked ? Hct(hct.hue(), hct.chroma(), 70.0) : hct;
}

// ---- Roles --------------------------------------------------------------------------------

// A value that changes with the contrast level: the four values are those at levels -1, 0, 0.5, 1.
struct ContrastCurve {
  double low;
  double normal;
  double medium;
  double high;

  double get(double level) const {
    if (level <= -1.0) return low;
    if (level < 0.0) return lerp(low, normal, level + 1.0);
    if (level < 0.5) return lerp(normal, medium, level / 0.5);
    if (level < 1.0) return lerp(medium, high, (level - 0.5) / 0.5);
    return high;
  }
};

enum class Pal { Primary, Secondary, Tertiary, Neutral, NeutralVariant, Error };
enum class Polarity { Darker, Lighter, Nearer, Farther };

struct ToneDelta {
  Role a;
  Role b;
  double delta;
  Polarity polarity;
  bool stayTogether;
};

using ToneFn = std::function<double(const DynamicScheme&)>;
using RoleFn = std::function<Role(const DynamicScheme&)>;

struct DynamicColor {
  std::string_view name;
  Pal palette = Pal::Neutral;
  ToneFn tone;
  bool isBackground = false;
  RoleFn background;  // empty: the colour is not adjusted for contrast
  std::optional<Role> secondBackground;
  std::optional<ContrastCurve> curve;
  std::optional<ToneDelta> pair;
};

const TonalPalette& paletteOf(Pal pal, const DynamicScheme& s) {
  switch (pal) {
    case Pal::Primary:
      return s.primary;
    case Pal::Secondary:
      return s.secondary;
    case Pal::Tertiary:
      return s.tertiary;
    case Pal::Neutral:
      return s.neutral;
    case Pal::NeutralVariant:
      return s.neutralVariant;
    case Pal::Error:
      return s.error;
  }
  return s.neutral;
}

bool isFidelity(const DynamicScheme& s) {
  return s.variant == Variant::Fidelity || s.variant == Variant::Content;
}
bool isMonochrome(const DynamicScheme& s) { return s.variant == Variant::Monochrome; }

bool tonePrefersLightForeground(double tone) { return std::round(tone) < 60; }

// The tone for text on `bgTone` that reaches `ratio`, preferring the side that suits the
// background (light text on tones below 60).
double foregroundTone(double bgTone, double ratio) {
  const double lighterTone = lighterUnsafe(bgTone, ratio);
  const double darkerTone = darkerUnsafe(bgTone, ratio);
  const double lighterRatio = ratioOfTones(lighterTone, bgTone);
  const double darkerRatio = ratioOfTones(darkerTone, bgTone);
  if (tonePrefersLightForeground(bgTone)) {
    const bool negligible =
        std::fabs(lighterRatio - darkerRatio) < 0.1 && lighterRatio < ratio && darkerRatio < ratio;
    return lighterRatio >= ratio || lighterRatio >= darkerRatio || negligible ? lighterTone
                                                                              : darkerTone;
  }
  return darkerRatio >= ratio || darkerRatio >= lighterRatio ? darkerTone : lighterTone;
}

// Fidelity secondary container: moves the tone until the palette shows the requested chroma.
double findDesiredChromaByTone(double hue, double chroma, double tone, bool byDecreasingTone) {
  double answer = tone;
  Hct closest(hue, chroma, tone);
  if (closest.chroma() < chroma) {
    double chromaPeak = closest.chroma();
    while (closest.chroma() < chroma) {
      answer += byDecreasingTone ? -1.0 : 1.0;
      const Hct potential(hue, chroma, answer);
      if (chromaPeak > potential.chroma()) break;
      if (std::fabs(potential.chroma() - chroma) < 0.4) break;
      if (std::fabs(potential.chroma() - chroma) < std::fabs(closest.chroma() - chroma)) {
        closest = potential;
      }
      chromaPeak = std::max(chromaPeak, potential.chroma());
    }
  }
  return answer;
}

Role highestSurface(const DynamicScheme& s) {
  return s.dark ? Role::SurfaceBright : Role::SurfaceDim;
}

class Table {
 public:
  Table();
  const DynamicColor& at(Role role) const { return colors_[static_cast<size_t>(role)]; }

 private:
  DynamicColor& slot(Role role) { return colors_[static_cast<size_t>(role)]; }
  void surface(Role role, std::string_view name, Pal pal, ToneFn tone);
  // A role on a background, with a contrast curve and an optional tone-delta pair.
  void onBackground(Role role, std::string_view name, Pal pal, ToneFn tone, bool isBackground,
                    RoleFn background, ContrastCurve curve,
                    std::optional<ToneDelta> pair = std::nullopt,
                    std::optional<Role> second = std::nullopt);

  std::array<DynamicColor, static_cast<size_t>(Role::Count_)> colors_;
};

const Table& table();

double toneOfRaw(Role role, const DynamicScheme& s) { return table().at(role).tone(s); }

RoleFn fixedRole(Role role) {
  return [role](const DynamicScheme&) { return role; };
}

ToneFn constant(double tone) {
  return [tone](const DynamicScheme&) { return tone; };
}

ToneFn lightDark(double light, double dark) {
  return [light, dark](const DynamicScheme& s) { return s.dark ? dark : light; };
}

ToneFn curved(ContrastCurve light, ContrastCurve dark) {
  return [light, dark](const DynamicScheme& s) { return (s.dark ? dark : light).get(s.contrast); };
}

void Table::surface(Role role, std::string_view name, Pal pal, ToneFn tone) {
  DynamicColor& c = slot(role);
  c.name = name;
  c.palette = pal;
  c.tone = std::move(tone);
  c.isBackground = true;
}

void Table::onBackground(Role role, std::string_view name, Pal pal, ToneFn tone, bool isBackground,
                         RoleFn background, ContrastCurve curve, std::optional<ToneDelta> pair,
                         std::optional<Role> second) {
  DynamicColor& c = slot(role);
  c.name = name;
  c.palette = pal;
  c.tone = std::move(tone);
  c.isBackground = isBackground;
  c.background = std::move(background);
  c.secondBackground = second;
  c.curve = curve;
  c.pair = pair;
}

Table::Table() {
  // Palette key colours (Android compatibility): the key tone of each palette.
  auto keyTone = [](Pal pal) {
    return [pal](const DynamicScheme& s) { return paletteOf(pal, s).keyColor().tone(); };
  };
  auto key = [&](Role role, std::string_view name, Pal pal) {
    DynamicColor& c = slot(role);
    c.name = name;
    c.palette = pal;
    c.tone = keyTone(pal);
  };
  key(Role::PrimaryPaletteKeyColor, "primary_paletteKeyColor", Pal::Primary);
  key(Role::SecondaryPaletteKeyColor, "secondary_paletteKeyColor", Pal::Secondary);
  key(Role::TertiaryPaletteKeyColor, "tertiary_paletteKeyColor", Pal::Tertiary);
  key(Role::NeutralPaletteKeyColor, "neutral_paletteKeyColor", Pal::Neutral);
  key(Role::NeutralVariantPaletteKeyColor, "neutral_variant_paletteKeyColor", Pal::NeutralVariant);

  // Surfaces: fixed tones, no contrast adjustment against another colour.
  surface(Role::Background, "background", Pal::Neutral, lightDark(98.0, 6.0));
  surface(Role::Surface, "surface", Pal::Neutral, lightDark(98.0, 6.0));
  surface(Role::SurfaceDim, "surfaceDim", Pal::Neutral,
          curved({87.0, 87.0, 80.0, 75.0}, {6.0, 6.0, 6.0, 6.0}));
  surface(Role::SurfaceBright, "surfaceBright", Pal::Neutral,
          curved({98.0, 98.0, 98.0, 98.0}, {24.0, 24.0, 29.0, 34.0}));
  surface(Role::SurfaceContainerLowest, "surfaceContainerLowest", Pal::Neutral,
          curved({100.0, 100.0, 100.0, 100.0}, {4.0, 4.0, 2.0, 0.0}));
  surface(Role::SurfaceContainerLow, "surfaceContainerLow", Pal::Neutral,
          curved({96.0, 96.0, 96.0, 95.0}, {10.0, 10.0, 11.0, 12.0}));
  surface(Role::SurfaceContainer, "surfaceContainer", Pal::Neutral,
          curved({94.0, 94.0, 92.0, 90.0}, {12.0, 12.0, 16.0, 20.0}));
  surface(Role::SurfaceContainerHigh, "surfaceContainerHigh", Pal::Neutral,
          curved({92.0, 92.0, 88.0, 85.0}, {17.0, 17.0, 21.0, 25.0}));
  surface(Role::SurfaceContainerHighest, "surfaceContainerHighest", Pal::Neutral,
          curved({90.0, 90.0, 84.0, 80.0}, {22.0, 22.0, 26.0, 30.0}));
  surface(Role::SurfaceVariant, "surfaceVariant", Pal::NeutralVariant, lightDark(90.0, 30.0));
  surface(Role::SurfaceTint, "surfaceTint", Pal::Primary, lightDark(40.0, 80.0));

  const ContrastCurve kText{4.5, 7.0, 11.0, 21.0};     // on a coloured or surface background
  const ContrastCurve kSubtle{3.0, 4.5, 7.0, 11.0};    // on containers, variant text
  const ContrastCurve kAccent{3.0, 4.5, 7.0, 7.0};     // accent fills on a surface
  const ContrastCurve kContainer{1.0, 1.0, 3.0, 4.5};  // containers on a surface

  onBackground(Role::OnBackground, "onBackground", Pal::Neutral, lightDark(10.0, 90.0), false,
               fixedRole(Role::Background), {3.0, 3.0, 4.5, 7.0});
  onBackground(Role::OnSurface, "onSurface", Pal::Neutral, lightDark(10.0, 90.0), false,
               highestSurface, kText);
  onBackground(Role::OnSurfaceVariant, "onSurfaceVariant", Pal::NeutralVariant,
               lightDark(30.0, 80.0), false, highestSurface, kSubtle);
  surface(Role::InverseSurface, "inverseSurface", Pal::Neutral, lightDark(20.0, 90.0));
  slot(Role::InverseSurface).isBackground = false;
  onBackground(Role::InverseOnSurface, "inverseOnSurface", Pal::Neutral, lightDark(95.0, 20.0),
               false, fixedRole(Role::InverseSurface), kText);
  onBackground(Role::Outline, "outline", Pal::NeutralVariant, lightDark(50.0, 60.0), false,
               highestSurface, {1.5, 3.0, 4.5, 7.0});
  onBackground(Role::OutlineVariant, "outlineVariant", Pal::NeutralVariant, lightDark(80.0, 30.0),
               false, highestSurface, {1.0, 1.0, 3.0, 4.5});
  surface(Role::Shadow, "shadow", Pal::Neutral, constant(0.0));
  slot(Role::Shadow).isBackground = false;
  surface(Role::Scrim, "scrim", Pal::Neutral, constant(0.0));
  slot(Role::Scrim).isBackground = false;

  // The accent families. `tone` entries are the starting tones the contrast logic then adjusts.
  onBackground(
      Role::Primary, "primary", Pal::Primary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 100.0 : 0.0;
        return s.dark ? 80.0 : 40.0;
      },
      true, highestSurface, kAccent,
      ToneDelta{Role::PrimaryContainer, Role::Primary, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnPrimary, "onPrimary", Pal::Primary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 10.0 : 90.0;
        return s.dark ? 20.0 : 100.0;
      },
      false, fixedRole(Role::Primary), kText);
  onBackground(
      Role::PrimaryContainer, "primaryContainer", Pal::Primary,
      [](const DynamicScheme& s) {
        if (isFidelity(s)) return s.source.tone();
        if (isMonochrome(s)) return s.dark ? 85.0 : 25.0;
        return s.dark ? 30.0 : 90.0;
      },
      true, highestSurface, kContainer,
      ToneDelta{Role::PrimaryContainer, Role::Primary, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnPrimaryContainer, "onPrimaryContainer", Pal::Primary,
      [](const DynamicScheme& s) {
        if (isFidelity(s)) return foregroundTone(toneOfRaw(Role::PrimaryContainer, s), 4.5);
        if (isMonochrome(s)) return s.dark ? 0.0 : 100.0;
        return s.dark ? 90.0 : 30.0;
      },
      false, fixedRole(Role::PrimaryContainer), kSubtle);
  onBackground(Role::InversePrimary, "inversePrimary", Pal::Primary, lightDark(80.0, 40.0), false,
               fixedRole(Role::InverseSurface), kAccent);

  onBackground(
      Role::Secondary, "secondary", Pal::Secondary,
      [](const DynamicScheme& s) { return s.dark ? 80.0 : 40.0; }, true, highestSurface, kAccent,
      ToneDelta{Role::SecondaryContainer, Role::Secondary, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnSecondary, "onSecondary", Pal::Secondary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 10.0 : 100.0;
        return s.dark ? 20.0 : 100.0;
      },
      false, fixedRole(Role::Secondary), kText);
  onBackground(
      Role::SecondaryContainer, "secondaryContainer", Pal::Secondary,
      [](const DynamicScheme& s) {
        const double initial = s.dark ? 30.0 : 90.0;
        if (isMonochrome(s)) return s.dark ? 30.0 : 85.0;
        if (!isFidelity(s)) return initial;
        return findDesiredChromaByTone(s.secondary.hue(), s.secondary.chroma(), initial, !s.dark);
      },
      true, highestSurface, kContainer,
      ToneDelta{Role::SecondaryContainer, Role::Secondary, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnSecondaryContainer, "onSecondaryContainer", Pal::Secondary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 90.0 : 10.0;
        if (!isFidelity(s)) return s.dark ? 90.0 : 30.0;
        return foregroundTone(toneOfRaw(Role::SecondaryContainer, s), 4.5);
      },
      false, fixedRole(Role::SecondaryContainer), kSubtle);

  onBackground(
      Role::Tertiary, "tertiary", Pal::Tertiary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 90.0 : 25.0;
        return s.dark ? 80.0 : 40.0;
      },
      true, highestSurface, kAccent,
      ToneDelta{Role::TertiaryContainer, Role::Tertiary, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnTertiary, "onTertiary", Pal::Tertiary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 10.0 : 90.0;
        return s.dark ? 20.0 : 100.0;
      },
      false, fixedRole(Role::Tertiary), kText);
  onBackground(
      Role::TertiaryContainer, "tertiaryContainer", Pal::Tertiary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 60.0 : 49.0;
        if (!isFidelity(s)) return s.dark ? 30.0 : 90.0;
        return fixIfDisliked(Hct(s.tertiary.tone(s.source.tone()))).tone();
      },
      true, highestSurface, kContainer,
      ToneDelta{Role::TertiaryContainer, Role::Tertiary, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnTertiaryContainer, "onTertiaryContainer", Pal::Tertiary,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 0.0 : 100.0;
        if (!isFidelity(s)) return s.dark ? 90.0 : 30.0;
        return foregroundTone(toneOfRaw(Role::TertiaryContainer, s), 4.5);
      },
      false, fixedRole(Role::TertiaryContainer), kSubtle);

  onBackground(Role::Error, "error", Pal::Error, lightDark(40.0, 80.0), true, highestSurface,
               kAccent,
               ToneDelta{Role::ErrorContainer, Role::Error, 10.0, Polarity::Nearer, false});
  onBackground(Role::OnError, "onError", Pal::Error, lightDark(100.0, 20.0), false,
               fixedRole(Role::Error), kText);
  onBackground(Role::ErrorContainer, "errorContainer", Pal::Error, lightDark(90.0, 30.0), true,
               highestSurface, kContainer,
               ToneDelta{Role::ErrorContainer, Role::Error, 10.0, Polarity::Nearer, false});
  onBackground(
      Role::OnErrorContainer, "onErrorContainer", Pal::Error,
      [](const DynamicScheme& s) {
        if (isMonochrome(s)) return s.dark ? 90.0 : 10.0;
        return s.dark ? 90.0 : 30.0;
      },
      false, fixedRole(Role::ErrorContainer), kSubtle);

  // Fixed roles keep their tones in light and dark; each family is a lighter/darker pair.
  struct Fixed {
    Role fixed;
    Role dim;
    Role on;
    Role onVariant;
    std::string_view fixedName;
    std::string_view dimName;
    std::string_view onName;
    std::string_view onVariantName;
    Pal pal;
  };
  const Fixed families[] = {
      {Role::PrimaryFixed, Role::PrimaryFixedDim, Role::OnPrimaryFixed, Role::OnPrimaryFixedVariant,
       "primaryFixed", "primaryFixedDim", "onPrimaryFixed", "onPrimaryFixedVariant", Pal::Primary},
      {Role::SecondaryFixed, Role::SecondaryFixedDim, Role::OnSecondaryFixed,
       Role::OnSecondaryFixedVariant, "secondaryFixed", "secondaryFixedDim", "onSecondaryFixed",
       "onSecondaryFixedVariant", Pal::Secondary},
      {Role::TertiaryFixed, Role::TertiaryFixedDim, Role::OnTertiaryFixed,
       Role::OnTertiaryFixedVariant, "tertiaryFixed", "tertiaryFixedDim", "onTertiaryFixed",
       "onTertiaryFixedVariant", Pal::Tertiary},
  };
  for (const Fixed& f : families) {
    const bool secondary = f.pal == Pal::Secondary;
    const ToneDelta pair{f.fixed, f.dim, 10.0, Polarity::Lighter, true};
    onBackground(
        f.fixed, f.fixedName, f.pal,
        [secondary](const DynamicScheme& s) {
          return isMonochrome(s) ? (secondary ? 80.0 : 40.0) : 90.0;
        },
        true, highestSurface, kContainer, pair);
    onBackground(
        f.dim, f.dimName, f.pal,
        [secondary](const DynamicScheme& s) {
          return isMonochrome(s) ? (secondary ? 70.0 : 30.0) : 80.0;
        },
        true, highestSurface, kContainer, pair);
    onBackground(
        f.on, f.onName, f.pal,
        [secondary](const DynamicScheme& s) {
          return secondary ? 10.0 : (isMonochrome(s) ? 100.0 : 10.0);
        },
        false, fixedRole(f.dim), kText, std::nullopt, f.fixed);
    onBackground(
        f.onVariant, f.onVariantName, f.pal,
        [secondary](const DynamicScheme& s) {
          if (secondary) return isMonochrome(s) ? 25.0 : 30.0;
          return isMonochrome(s) ? 90.0 : 30.0;
        },
        false, fixedRole(f.dim), kSubtle, std::nullopt, f.fixed);
  }
}

const Table& table() {
  static const Table instance;
  return instance;
}

// Resolves the tone of a role: its starting tone, then the adjustments that keep it at the contrast
// ratio its curve asks for against its background(s), and the tone-delta rules of its pair.
double resolveTone(Role role, const DynamicScheme& s) {
  const DynamicColor& self = table().at(role);
  const bool decreasingContrast = s.contrast < 0;

  if (self.pair) {
    // Case 1: a pair of colours with a tone-delta constraint (primary and primaryContainer...).
    const ToneDelta& pair = *self.pair;
    const double bgTone = resolveTone(self.background(s), s);
    const bool aIsNearer = pair.polarity == Polarity::Nearer ||
                           (pair.polarity == Polarity::Lighter && !s.dark) ||
                           (pair.polarity == Polarity::Darker && s.dark);
    const Role nearerRole = aIsNearer ? pair.a : pair.b;
    const Role fartherRole = aIsNearer ? pair.b : pair.a;
    const bool amNearer = role == nearerRole;
    const double expansionDir = s.dark ? 1.0 : -1.0;
    const double delta = pair.delta;

    const double nContrast = table().at(nearerRole).curve->get(s.contrast);
    const double fContrast = table().at(fartherRole).curve->get(s.contrast);

    // A tone that already has enough contrast is not adjusted.
    const double nInitial = toneOfRaw(nearerRole, s);
    double nTone =
        ratioOfTones(bgTone, nInitial) >= nContrast ? nInitial : foregroundTone(bgTone, nContrast);
    const double fInitial = toneOfRaw(fartherRole, s);
    double fTone =
        ratioOfTones(bgTone, fInitial) >= fContrast ? fInitial : foregroundTone(bgTone, fContrast);
    if (decreasingContrast) {
      nTone = foregroundTone(bgTone, nContrast);
      fTone = foregroundTone(bgTone, fContrast);
    }

    if ((fTone - nTone) * expansionDir < delta) {
      // Expand the farther tone to match the delta, then contract the nearer one if still short.
      fTone = std::clamp(nTone + delta * expansionDir, 0.0, 100.0);
      if ((fTone - nTone) * expansionDir < delta) {
        nTone = std::clamp(fTone - delta * expansionDir, 0.0, 100.0);
      }
    }

    // Avoid the awkward 50-59 zone, where neither light nor dark text reads well.
    if (50 <= nTone && nTone < 60) {
      if (expansionDir > 0) {
        nTone = 60;
        fTone = std::max(fTone, nTone + delta * expansionDir);
      } else {
        nTone = 49;
        fTone = std::min(fTone, nTone + delta * expansionDir);
      }
    } else if (50 <= fTone && fTone < 60) {
      if (pair.stayTogether) {
        if (expansionDir > 0) {
          nTone = 60;
          fTone = std::max(fTone, nTone + delta * expansionDir);
        } else {
          nTone = 49;
          fTone = std::min(fTone, nTone + delta * expansionDir);
        }
      } else {
        fTone = expansionDir > 0 ? 60 : 49;
      }
    }
    return amNearer ? nTone : fTone;
  }

  // Case 2: a colour on its own, solved against its background.
  double answer = self.tone(s);
  if (!self.background) return answer;

  const double bgTone = resolveTone(self.background(s), s);
  const double desiredRatio = self.curve->get(s.contrast);
  if (ratioOfTones(bgTone, answer) < desiredRatio) answer = foregroundTone(bgTone, desiredRatio);
  if (decreasingContrast) answer = foregroundTone(bgTone, desiredRatio);

  if (self.isBackground && 50 <= answer && answer < 60) {
    answer = ratioOfTones(49, bgTone) >= desiredRatio ? 49 : 60;
  }

  if (self.secondBackground) {
    // Case 3: text on two backgrounds (a fixed colour and its dim).
    const double bgTone1 = bgTone;
    const double bgTone2 = resolveTone(*self.secondBackground, s);
    const double upper = std::max(bgTone1, bgTone2);
    const double lower = std::min(bgTone1, bgTone2);
    if (ratioOfTones(upper, answer) >= desiredRatio &&
        ratioOfTones(lower, answer) >= desiredRatio) {
      return answer;
    }
    const double lightOption = lighter(upper, desiredRatio);
    const double darkOption = darker(lower, desiredRatio);
    const bool prefersLight =
        tonePrefersLightForeground(bgTone1) || tonePrefersLightForeground(bgTone2);
    if (prefersLight) return lightOption < 0 ? 100 : lightOption;
    if ((lightOption != -1) != (darkOption != -1))
      return lightOption != -1 ? lightOption : darkOption;
    return darkOption < 0 ? 0 : darkOption;
  }
  return answer;
}

}  // namespace

std::string_view variantName(Variant variant) {
  for (const VariantName& v : kVariantNames) {
    if (v.variant == variant) return v.name;
  }
  return "tonalspot";
}

std::optional<Variant> parseVariant(std::string_view name) {
  std::string key;
  for (char ch : name) {
    if (ch == '_' || ch == '-' || ch == ' ') continue;
    key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  for (const VariantName& v : kVariantNames) {
    if (v.name == key) return v.variant;
  }
  return std::nullopt;
}

const std::vector<Variant>& allVariants() {
  static const std::vector<Variant> variants = [] {
    std::vector<Variant> out;
    for (const VariantName& v : kVariantNames) out.push_back(v.variant);
    return out;
  }();
  return variants;
}

DynamicScheme makeScheme(Argb seed, Variant variant, bool dark, double contrast) {
  DynamicScheme s;
  s.source = Hct(seed);
  s.variant = variant;
  s.dark = dark;
  s.contrast = std::clamp(contrast, -1.0, 1.0);
  const double hue = s.source.hue();
  const double chroma = s.source.chroma();
  switch (variant) {
    case Variant::TonalSpot:
      s.primary = {hue, 36.0};
      s.secondary = {hue, 16.0};
      s.tertiary = {sanitizeDegrees(hue + 60.0), 24.0};
      s.neutral = {hue, 6.0};
      s.neutralVariant = {hue, 8.0};
      break;
    case Variant::Vibrant:
      s.primary = {hue, 200.0};
      s.secondary = {rotatedHue(s.source, {0, 41, 61, 101, 131, 181, 251, 301, 360},
                                {18, 15, 10, 12, 15, 18, 15, 12, 12}),
                     24.0};
      s.tertiary = {rotatedHue(s.source, {0, 41, 61, 101, 131, 181, 251, 301, 360},
                               {35, 30, 20, 25, 30, 35, 30, 25, 25}),
                    32.0};
      s.neutral = {hue, 10.0};
      s.neutralVariant = {hue, 12.0};
      break;
    case Variant::Expressive:
      s.primary = {sanitizeDegrees(hue + 240.0), 40.0};
      s.secondary = {rotatedHue(s.source, {0, 21, 51, 121, 151, 191, 271, 321, 360},
                                {45, 95, 45, 20, 45, 90, 45, 45, 45}),
                     24.0};
      s.tertiary = {rotatedHue(s.source, {0, 21, 51, 121, 151, 191, 271, 321, 360},
                               {120, 120, 20, 45, 20, 15, 20, 120, 120}),
                    32.0};
      s.neutral = {sanitizeDegrees(hue + 15.0), 8.0};
      s.neutralVariant = {sanitizeDegrees(hue + 15.0), 12.0};
      break;
    case Variant::Neutral:
      s.primary = {hue, 12.0};
      s.secondary = {hue, 8.0};
      s.tertiary = {hue, 16.0};
      s.neutral = {hue, 2.0};
      s.neutralVariant = {hue, 2.0};
      break;
    case Variant::Monochrome:
      s.primary = s.secondary = s.tertiary = s.neutral = s.neutralVariant = {hue, 0.0};
      break;
    case Variant::Fidelity:
    case Variant::Content: {
      s.primary = {hue, chroma};
      s.secondary = {hue, std::max(chroma - 32.0, chroma * 0.5)};
      const TemperatureCache temperature(s.source);
      s.tertiary = TonalPalette(variant == Variant::Fidelity
                                    ? fixIfDisliked(temperature.complement())
                                    : fixIfDisliked(temperature.analogousColors(3, 6).at(2)));
      s.neutral = {hue, chroma / 8.0};
      s.neutralVariant = {hue, chroma / 8.0 + 4.0};
      break;
    }
    case Variant::Rainbow:
      s.primary = {hue, 48.0};
      s.secondary = {hue, 16.0};
      s.tertiary = {sanitizeDegrees(hue + 60.0), 24.0};
      s.neutral = {hue, 0.0};
      s.neutralVariant = {hue, 0.0};
      break;
    case Variant::FruitSalad:
      s.primary = {sanitizeDegrees(hue - 50.0), 48.0};
      s.secondary = {sanitizeDegrees(hue - 50.0), 36.0};
      s.tertiary = {hue, 36.0};
      s.neutral = {hue, 10.0};
      s.neutralVariant = {hue, 16.0};
      break;
  }
  return s;
}

std::string_view roleName(Role role) {
  if (role >= Role::Count_) return {};
  return table().at(role).name;
}

Argb roleColor(Role role, const DynamicScheme& scheme) {
  if (role >= Role::Count_) return 0;
  return paletteOf(table().at(role).palette, scheme).tone(resolveTone(role, scheme));
}

double roleTone(Role role, const DynamicScheme& scheme) {
  if (role >= Role::Count_) return 0.0;
  return resolveTone(role, scheme);
}

}  // namespace waku::color_scheme

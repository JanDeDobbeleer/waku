#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "features/color_scheme/tonal_palette.h"

namespace waku::color_scheme {

// The scheme variants of material-color-utilities (the 2021 colour spec) that celeste offers.
enum class Variant {
  TonalSpot,
  Vibrant,
  Expressive,
  Neutral,
  Monochrome,
  Fidelity,
  Content,
  Rainbow,
  FruitSalad,
};

// Lower-case name without separators ("tonalspot", "fruitsalad"), the form `variantName` returns.
// parseVariant also accepts "tonal_spot", "fruit-salad" and any case.
std::string_view variantName(Variant variant);
std::optional<Variant> parseVariant(std::string_view name);
const std::vector<Variant>& allVariants();

// A seed colour turned into the five tonal palettes of a variant, for light or dark UI at a
// contrast level in -1 (reduced) .. 0 (standard) .. 1 (high).
struct DynamicScheme {
  Hct source;
  Variant variant = Variant::TonalSpot;
  bool dark = false;
  double contrast = 0.0;
  TonalPalette primary;
  TonalPalette secondary;
  TonalPalette tertiary;
  TonalPalette neutral;
  TonalPalette neutralVariant;
  TonalPalette error{25.0, 84.0};
};

DynamicScheme makeScheme(Argb seed, Variant variant, bool dark, double contrast = 0.0);

// The Material 3 colour roles, in the order the reference library lists them.
enum class Role {
  PrimaryPaletteKeyColor,
  SecondaryPaletteKeyColor,
  TertiaryPaletteKeyColor,
  NeutralPaletteKeyColor,
  NeutralVariantPaletteKeyColor,
  Background,
  OnBackground,
  Surface,
  SurfaceDim,
  SurfaceBright,
  SurfaceContainerLowest,
  SurfaceContainerLow,
  SurfaceContainer,
  SurfaceContainerHigh,
  SurfaceContainerHighest,
  OnSurface,
  SurfaceVariant,
  OnSurfaceVariant,
  InverseSurface,
  InverseOnSurface,
  Outline,
  OutlineVariant,
  Shadow,
  Scrim,
  SurfaceTint,
  Primary,
  OnPrimary,
  PrimaryContainer,
  OnPrimaryContainer,
  InversePrimary,
  Secondary,
  OnSecondary,
  SecondaryContainer,
  OnSecondaryContainer,
  Tertiary,
  OnTertiary,
  TertiaryContainer,
  OnTertiaryContainer,
  Error,
  OnError,
  ErrorContainer,
  OnErrorContainer,
  PrimaryFixed,
  PrimaryFixedDim,
  OnPrimaryFixed,
  OnPrimaryFixedVariant,
  SecondaryFixed,
  SecondaryFixedDim,
  OnSecondaryFixed,
  OnSecondaryFixedVariant,
  TertiaryFixed,
  TertiaryFixedDim,
  OnTertiaryFixed,
  OnTertiaryFixedVariant,
  Count_,
};

// The role's name as celeste and materialyoucolor write it: "primary", "onPrimary",
// "surfaceContainerHigh", "primaryFixedDim", "primary_paletteKeyColor".
std::string_view roleName(Role role);

// The role's colour in a scheme.
Argb roleColor(Role role, const DynamicScheme& scheme);

// The tone a role resolves to, after the contrast-curve and tone-delta adjustments.
double roleTone(Role role, const DynamicScheme& scheme);

}  // namespace waku::color_scheme

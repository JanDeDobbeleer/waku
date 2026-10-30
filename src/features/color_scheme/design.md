# color_scheme: Material 3 dynamic colour

Current design of the capability. A pure C++ engine, one QML singleton, no external tool. It gives a
shell a Material 3 palette from a colour or a wallpaper, read as `primary`, `onSurface` and so on. Names
and prefixes a particular shell expects are that shell's business, in its own QML.

## Files

| File | Content |
|---|---|
| `color_math.{h,cpp}` | ARGB helpers, sRGB linearisation, L* and Y, CIE Lab, WCAG contrast on tones (`ratioOfTones`, `lighter`, `darker`). |
| `hct.{h,cpp}` | CAM16 under the default viewing conditions, the HCT solver (`solveToArgb`: Newton on J, falls back to a bisection on the RGB cube edge) and the `Hct` value. |
| `tonal_palette.{h,cpp}` | `TonalPalette` (hue, chroma, tone to ARGB, key colour) and `TemperatureCache` (complement and analogous colours, used by Fidelity and Content). |
| `dynamic_scheme.{h,cpp}` | The nine `Variant`s, `makeScheme`, and the Material 3 role table with contrast curves and tone-delta pairs (`roleColor`, `roleTone`, `roleName`). |
| `quantizer.{h,cpp}` | Wu, weighted k-means (WSMeans), Celebi (Wu then WSMeans) and `rankedSuggestions` (Score). |
| `image_source.{h,cpp}` | `readImageScaled`, `seedColorFromImage` (128 px, 128 colours, Score top 1) and `meanLuminance`. |
| `qml/color_scheme_service.{h,cpp}` | The `ColorScheme` singleton in `wabar.services`. |
| `qml/color_scheme_types.h` | The `scheme` value type (`SchemeValue`). |

## Behaviour

- The engine is a re-implementation of material-color-utilities (Apache-2.0, see
  `THIRD_PARTY_NOTICES.md`), the 2021 colour spec: the one Android, Flutter and materialyoucolor
  (which caelestia uses) ship by default.
- Variants: `tonalspot`, `vibrant`, `expressive`, `neutral`, `monochrome`, `fidelity`, `content`,
  `rainbow`, `fruitsalad`. A name is matched without case, `_`, `-` or spaces.
- `contrast` runs from -1 (reduced) over 0 (standard) to 1 (high) and is clamped to that range.
- Role names follow caelestia and materialyoucolor: `primary`, `onPrimary`, `primaryContainer`,
  `surface`, `surfaceContainerLowest` to `surfaceContainerHighest`, `outline`, `outlineVariant`,
  `inverseSurface`, `scrim`, `shadow`, `surfaceTint`, `primaryFixed`, `onPrimaryFixedVariant`, the
  `secondary`, `tertiary` and `error` families, and `primary_paletteKeyColor` and its four siblings.
  `colors` holds each role once, by that name.
- `ColorScheme.fromColor(color, variant, dark, contrast)` answers at once. `variant` defaults to
  `tonalspot`, `dark` to `true`, `contrast` to 0. A bad variant or colour gives `valid: false`.
- `ColorScheme.fromImage(path, variant, dark, contrast)` returns at once and emits
  `fromImageFinished(path, scheme)` or `fromImageFailed(path, error)`, once. `path` may be a
  `file:///` URL. The seed is the best-scoring colour of the image (shrunk to 128 px, 128 colours).
  A grey image gives Google blue (`#4285F4`), as Score does.
- `ColorScheme.generate(source, ...)` takes a colour (answers at once) or an existing image file
  (starts `fromImage`, returns an invalid scheme, the result arrives by signal).
- `ColorScheme.luminance(path)` is the mean Rec. 709 luma of the gamma-encoded sRGB channels, 0..1,
  decoded at 64 px on the calling thread; -1 when the file cannot be read.

## Decisions

- **Dynamic colour, not `Scheme`.** The table is the DynamicScheme of the reference, so TonalSpot
  gives primary `#65558F` for the seed `#6750A4`, not the baseline `#6750A4`. The baseline is the
  older CorePalette (primary chroma 48) and the tests check both.
- **Derived tables.** The reference lists 255 "critical planes" for the HCT solver. They are
  `linearized(i + 0.5)`, so they are computed once instead of copied.
- **Role table.** One `DynamicColor` per role (palette, tone function, background, contrast curve,
  optional tone-delta pair) and one `resolveTone`, as the reference. No role is special-cased.
- **Rotated hue.** The hue ranges are `breakpoint[i] <= hue < breakpoint[i + 1]` (the TypeScript
  library and its documentation); the C++ library has `<` on both ends, which leaves a source hue
  that equals a breakpoint unrotated.
- **Repeatable k-means.** The reference seeds the initial pixel-to-cluster assignment with `rand()`
  and `srand(42688)`, which differs per C runtime. This port uses `std::mt19937(42688)`. With Wu's
  clusters as the start the result is the same colours in practice.
- **Luminance is gamma-encoded.** "Is the wallpaper light or dark" wants mid grey to read as 0.5,
  which linear-light luminance does not give. If a ported shell needs linear Y it is a one-line
  change in `meanLuminance`; the value caelestia's ImageAnalyser returns could not be checked here.
- **No `liveDesktop()`.** The service reads one file the caller names and touches nothing else.

## Dependencies

Qt Core, Gui (QImage, QImageReader) and `kernel` through the feature macro. No feature dependency.

## Open points

- caelestia also derives non-Material colours (terminal ANSI palette, success colour, blended
  variants of the seed). They are not part of material-color-utilities and are not here.
- The 2025 colour spec (`SpecVersion.SPEC_2025`) is not ported.
- `luminance()` decodes on the calling thread. A wallpaper change that must not stall the GUI
  thread can call `fromImage` first and read `luminance` once, or this grows an async twin.

## Tests

`src/features/color_scheme/tests/test_color_scheme.cpp` (`ColorScheme*`): CAM16, HCT, tonal palette
sweep, the Material 3 baseline, the monochrome tones, every variant at four contrast levels, the
quantizer, Score, image seeds and luminance, and the singleton from C++ and from QML. The vectors
come from the reference library's own tests and from the published Material 3 baseline; the
file header names the sources.

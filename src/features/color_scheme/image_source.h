#pragma once

#include <QImage>
#include <QString>
#include <optional>

#include "features/color_scheme/color_math.h"

namespace waku::color_scheme {

// Reads an image file, shrunk so neither side exceeds `maxSide` pixels (the decoder scales while
// reading where the format allows it). Returns a null image and sets `error` on failure.
// Safe on any thread.
QImage readImageScaled(const QString& path, int maxSide, QString* error = nullptr);

// The theme seed colour of an image: the image is shrunk to 128 pixels, quantized to 128 colours
// (Celebi) and the best-scoring colour is picked (Score). Falls back to Google blue (0xff4285f4)
// for an image with no colourful pixel, such as a grey wallpaper. Nullopt for a null image.
std::optional<Argb> seedColorFromImage(const QImage& image);

// Mean luminance of the image in 0..1: the Rec. 709 weights (0.2126, 0.7152, 0.0722) applied to the
// gamma-encoded sRGB channels, averaged over the opaque pixels. 0 for a null image. This is the
// "is the wallpaper light or dark" figure, so it is deliberately not linear light: mid grey reads
// as 0.5.
double meanLuminance(const QImage& image);

}  // namespace waku::color_scheme

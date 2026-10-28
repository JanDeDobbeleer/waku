#pragma once

#include <QImage>

namespace yasb::widgets {

// Pillow Image.resize(size, Image.LANCZOS): separable Lanczos-3 resampling that widens its kernel
// when downscaling. RGBA input is resampled with premultiplied alpha, as Pillow does. Returns a
// Format_RGBA8888 image; a null image when the input or target size is empty.
QImage lanczosResize(const QImage& source, int width, int height);

}  // namespace yasb::widgets

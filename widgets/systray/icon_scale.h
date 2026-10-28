#pragma once

#include <QImage>

namespace yasb::widgets {

// Python's validate_icon_data resizes any tray icon that is not 32x32 with
// `image.resize((32, 32), Image.Resampling.LANCZOS).filter(ImageFilter.SHARPEN)` (Pillow).
// This follows Pillow's algorithm: premultiplied RGBA, separable Lanczos (a = 3) with 8-bit
// fixed-point coefficients, horizontal pass first, then the 3x3 SHARPEN kernel on all four bands
// leaving the one-pixel border untouched. `source` must be Format_RGBA8888 (other formats are
// converted). A 32x32 image is returned unchanged.
QImage scaleTrayIcon(const QImage& source, int size = 32);

// The two stages, exposed for tests.
QImage lanczosResize(const QImage& source, int width, int height);
QImage sharpen(const QImage& source);

}  // namespace yasb::widgets

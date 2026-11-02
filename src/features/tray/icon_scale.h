#pragma once

#include <QByteArray>
#include <QImage>

#include "features/tray/tray_types.h"

namespace waku::tray {

// The size a tray icon is shown at (Python's validate_icon_data).
inline constexpr int kTrayIconSize = 32;

// Python's validate_icon_data resizes any tray icon that is not 32x32 with
// `image.resize((32, 32), Image.Resampling.LANCZOS).filter(ImageFilter.SHARPEN)` (Pillow).
// This follows Pillow's algorithm: premultiplied RGBA, separable Lanczos (a = 3) with 8-bit
// fixed-point coefficients, horizontal pass first, then the 3x3 SHARPEN kernel on all four bands
// leaving the one-pixel border untouched. `source` must be Format_RGBA8888 (other formats are
// converted). A 32x32 image is returned unchanged.
QImage scaleTrayIcon(const QImage& source, int size = 32);

// The tray image of an icon: its RGBA through scaleTrayIcon, or the host's `scaledRgba` when it
// already did that. Null when the data carries no usable pixels.
QImage trayIconImage(const TrayIconData& data);
// scaleTrayIcon of the icon's RGBA as 32x32 RGBA8888 bytes, for a host to run off the GUI thread
// and put in `scaledRgba`; empty when the icon is already 32x32 or has no usable pixels.
QByteArray scaledTrayIconRgba(const TrayIconData& data);

// The two stages, exposed for tests.
QImage lanczosScaleIcon(const QImage& source, int width, int height);
QImage sharpen(const QImage& source);

}  // namespace waku::tray

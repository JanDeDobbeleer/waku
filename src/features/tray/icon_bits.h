#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

#include "features/tray/tray_types.h"

namespace wabar::tray {

// The pixel conversions of an icon read with GetDIBits, kept free of Windows types so tests can
// feed them hand-made bitmaps. Every buffer is top-down, 4 bytes per pixel.

// A colour icon: `colorBgra` is hbmColor read as a 32 bpp DIB (B, G, R, A), `maskBgra` hbmMask read
// the same way (a set AND bit reads as 255), or empty when there is no mask. Returns RGBA8888 with
// straight alpha, as Windows stores icon pixels. When no pixel has a non-zero alpha the icon has no
// alpha channel (a classic 24 bpp or palette icon): the AND mask gives the transparency, and
// without a mask every pixel is opaque. Empty when a buffer is shorter than width * height * 4.
QByteArray colorIconToRgba(const QByteArray& colorBgra, const QByteArray& maskBgra, int width,
                           int height);

// A monochrome icon (no hbmColor): `maskBgra` is hbmMask, `height` * 2 rows read as 32 bpp, the AND
// mask above the XOR mask. AND 0 shows the XOR colour (black or white); AND 1 with XOR 0 is
// transparent; AND 1 with XOR 1 inverts the screen, drawn here as opaque white. Empty when the
// buffer is shorter than width * height * 2 * 4.
QByteArray monochromeIconToRgba(const QByteArray& maskBgra, int width, int height);

// Why the RGBA of `data` cannot show as an icon: no pixels, pixel data that does not fill
// iconWidth x iconHeight, or every pixel fully transparent. nullopt when it can.
std::optional<QString> trayIconPixelsProblem(const TrayIconData& data);

}  // namespace wabar::tray

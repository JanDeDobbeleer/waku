#include "features/tray/icon_bits.h"

namespace waku::tray {
namespace {

qsizetype pixelBytes(int width, int height) {
  if (width <= 0 || height <= 0) return 0;
  return qsizetype{4} * width * height;
}

}  // namespace

QByteArray colorIconToRgba(const QByteArray& colorBgra, const QByteArray& maskBgra, int width,
                           int height) {
  const qsizetype bytes = pixelBytes(width, height);
  if (bytes == 0 || colorBgra.size() < bytes) return {};
  bool hasAlpha = false;
  for (qsizetype i = 3; i < bytes; i += 4) {
    if (colorBgra[i] != 0) {
      hasAlpha = true;
      break;
    }
  }
  const bool useMask = !hasAlpha && maskBgra.size() >= bytes;
  QByteArray rgba(bytes, Qt::Uninitialized);
  for (qsizetype i = 0; i < bytes; i += 4) {
    rgba[i + 0] = colorBgra[i + 2];
    rgba[i + 1] = colorBgra[i + 1];
    rgba[i + 2] = colorBgra[i + 0];
    if (hasAlpha) {
      rgba[i + 3] = colorBgra[i + 3];
    } else if (useMask) {
      rgba[i + 3] = static_cast<unsigned char>(maskBgra[i]) == 255 ? '\x00' : '\xff';
    } else {
      rgba[i + 3] = '\xff';
    }
  }
  return rgba;
}

QByteArray monochromeIconToRgba(const QByteArray& maskBgra, int width, int height) {
  const qsizetype bytes = pixelBytes(width, height);
  if (bytes == 0 || maskBgra.size() < bytes * 2) return {};
  QByteArray rgba(bytes, Qt::Uninitialized);
  for (qsizetype i = 0; i < bytes; i += 4) {
    const bool andBit = static_cast<unsigned char>(maskBgra[i]) == 255;
    const bool xorBit = static_cast<unsigned char>(maskBgra[bytes + i]) == 255;
    const char value = xorBit ? '\xff' : '\x00';
    rgba[i + 0] = value;
    rgba[i + 1] = value;
    rgba[i + 2] = value;
    rgba[i + 3] = (andBit && !xorBit) ? '\x00' : '\xff';
  }
  return rgba;
}

std::optional<QString> trayIconPixelsProblem(const TrayIconData& data) {
  if (data.iconRgba.isEmpty()) return QStringLiteral("no icon pixels");
  const qsizetype bytes = pixelBytes(data.iconWidth, data.iconHeight);
  if (bytes == 0 || data.iconRgba.size() < bytes) {
    return QStringLiteral("%1 bytes of pixels for a %2x%3 icon")
        .arg(data.iconRgba.size())
        .arg(data.iconWidth)
        .arg(data.iconHeight);
  }
  for (qsizetype i = 3; i < bytes; i += 4) {
    if (data.iconRgba[i] != 0) return std::nullopt;
  }
  return QStringLiteral("all %1x%2 pixels are fully transparent")
      .arg(data.iconWidth)
      .arg(data.iconHeight);
}

}  // namespace waku::tray

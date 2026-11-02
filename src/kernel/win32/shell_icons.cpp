#include "kernel/win32/shell_icons.h"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl_core.h>

#include <algorithm>
#include <vector>

#include "kernel/win32/com_scope.h"

namespace waku::kernel {
namespace {

// A GetDIBits read of an icon bitmap as top-down 32-bit BGRA.
bool readBitmap(HBITMAP bitmap, int width, int height, std::vector<unsigned char>& pixels) {
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  pixels.assign(static_cast<size_t>(width) * height * 4, 0);
  HDC dc = GetDC(nullptr);
  const int lines =
      GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  return lines == height;
}

IconBitmap bitmapFromHicon(HICON icon) {
  IconBitmap result;
  if (!icon) return result;
  ICONINFO iconInfo = {};
  if (!GetIconInfo(icon, &iconInfo)) return result;
  BITMAP colorInfo = {};
  if (iconInfo.hbmColor && GetObjectW(iconInfo.hbmColor, sizeof(colorInfo), &colorInfo)) {
    const int width = colorInfo.bmWidth;
    const int height = colorInfo.bmHeight;
    std::vector<unsigned char> pixels;
    if (width > 0 && height > 0 && readBitmap(iconInfo.hbmColor, width, height, pixels)) {
      bool hasAlpha = false;
      for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
          hasAlpha = true;
          break;
        }
      }
      if (!hasAlpha && iconInfo.hbmMask) {
        // Old style icon: the AND mask says which pixels are transparent.
        std::vector<unsigned char> mask;
        if (readBitmap(iconInfo.hbmMask, width, height, mask)) {
          for (size_t i = 0; i < pixels.size(); i += 4) pixels[i + 3] = mask[i] ? 0 : 255;
          hasAlpha = true;
        }
      }
      if (hasAlpha) {
        result.width = width;
        result.height = height;
        result.bgra = QByteArray(reinterpret_cast<const char*>(pixels.data()),
                                 static_cast<qsizetype>(pixels.size()));
      }
    }
  }
  if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
  if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
  return result;
}

// The 32-bit premultiplied DIB IShellItemImageFactory hands back, as straight alpha.
IconBitmap bitmapFromShell(HBITMAP bitmap) {
  IconBitmap result;
  BITMAP info = {};
  if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 ||
      info.bmHeight <= 0) {
    return result;
  }
  std::vector<unsigned char> pixels;
  if (!readBitmap(bitmap, info.bmWidth, info.bmHeight, pixels)) return result;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    const unsigned alpha = pixels[i + 3];
    if (alpha != 0 && alpha != 255) {
      for (size_t c = 0; c < 3; ++c)
        pixels[i + c] = static_cast<unsigned char>(std::min(255u, pixels[i + c] * 255u / alpha));
    }
  }
  result.width = info.bmWidth;
  result.height = info.bmHeight;
  result.bgra = QByteArray(reinterpret_cast<const char*>(pixels.data()),
                           static_cast<qsizetype>(pixels.size()));
  return result;
}

IconBitmap shellIcon(const QString& parsingName, int size) {
  IShellItemImageFactory* factory = nullptr;
  if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(parsingName.utf16()), nullptr,
                                         IID_PPV_ARGS(&factory))) ||
      !factory) {
    return {};
  }
  HBITMAP bitmap = nullptr;
  IconBitmap result;
  if (SUCCEEDED(
          factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap))) {
    result = bitmapFromShell(bitmap);
  }
  if (bitmap) DeleteObject(bitmap);
  factory->Release();
  return result;
}

}  // namespace

IconBitmap shellItemIcon(const QString& parsingName, int size) {
  // An empty name would resolve to the desktop folder.
  if (parsingName.isEmpty()) return {};
  const ComScope com(ComApartment::MultiThreaded);
  return shellIcon(parsingName, size);
}

IconBitmap iconBitmapFromHicon(void* hicon) { return bitmapFromHicon(static_cast<HICON>(hicon)); }

}  // namespace waku::kernel

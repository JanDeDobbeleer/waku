#include "host/win32/backdrop.h"

#include <windows.h>
#include <dwmapi.h>

#include <QDebug>
#include <stdexcept>

#include "kernel/win32/windows_build.h"

#pragma comment(lib, "dwmapi.lib")

namespace wabar::host {
namespace {

constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kWindowCornerPreference = 33;
constexpr DWORD kBorderColor = 34;
constexpr DWORD kSystemBackdropType = 38;
constexpr DWORD kMicaEffect = 1029;
constexpr int kCornerRound = 2;
constexpr int kCornerRoundSmall = 3;
constexpr int kBackdropMainWindow = 2;
constexpr DWORD kColorDefault = 0xFFFFFFFF;
constexpr DWORD kColorNone = 0xFFFFFFFE;
constexpr int kWcaAccentPolicy = 19;
constexpr int kAccentBlurBehind = 3;
constexpr int kAccentAcrylicBlurBehind = 4;

struct AccentPolicy {
  int accentState;
  int accentFlags;
  DWORD gradientColor;
  int animationId;
};

struct CompositionAttribData {
  int attribute;
  void* data;
  ULONG sizeOfData;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, CompositionAttribData*);

void setAccentPolicy(HWND hwnd, int state, DWORD gradient) {
  static const auto fn = [] {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<SetWindowCompositionAttributeFn>(
                        GetProcAddress(user32, "SetWindowCompositionAttribute"))
                  : nullptr;
  }();
  if (!fn) throw std::runtime_error("SetWindowCompositionAttribute unavailable");
  AccentPolicy accent{state, 0, gradient, 0};
  CompositionAttribData data{kWcaAccentPolicy, &accent, sizeof(accent)};
  if (!fn(hwnd, &data)) throw std::runtime_error("SetWindowCompositionAttribute failed");
}

void setAttr(HWND hwnd, DWORD attr, const void* value, DWORD size) {
  const HRESULT hr = DwmSetWindowAttribute(hwnd, attr, value, size);
  if (FAILED(hr)) throw std::runtime_error("DwmSetWindowAttribute failed");
}

void setCornerPreference(HWND hwnd, int preference, const QString& borderColor) {
  setAttr(hwnd, kWindowCornerPreference, &preference, sizeof(preference));
  DWORD color = kColorDefault;
  if (borderColor == QLatin1String("None")) {
    color = kColorNone;
  } else if (borderColor.compare(QLatin1String("system"), Qt::CaseInsensitive) != 0) {
    color = hexToRgbaInt(borderColor);
  }
  setAttr(hwnd, kBorderColor, &color, sizeof(color));
}

HWND toHwnd(quintptr h) { return reinterpret_cast<HWND>(h); }

}  // namespace

std::uint32_t hexToRgbaInt(const QString& hex) {
  if (hex.size() != 9 || hex[0] != QLatin1Char('#'))
    throw std::invalid_argument("expected #RRGGBBAA");
  bool ok = false;
  const QString swapped = hex.mid(7, 2) + hex.mid(5, 2) + hex.mid(3, 2) + hex.mid(1, 2);
  const std::uint32_t v = swapped.toUInt(&ok, 16);
  if (!ok) throw std::invalid_argument("expected #RRGGBBAA");
  return v;
}

bool isMicaSupported() { return wabar::kernel::windowsBuild() >= 22000; }

void enableBlur(quintptr hwndValue, bool darkMode, bool roundCorners,
                const QString& roundCornersType, const QString& borderColor) {
  const HWND hwnd = toHwnd(hwndValue);
  if (!IsWindow(hwnd)) return;
  try {
    if (wabar::kernel::windowsBuild() >= 22000) {
      setAccentPolicy(hwnd, kAccentBlurBehind, 0x01202020);
      if (darkMode) {
        const int one = 1;
        setAttr(hwnd, kUseImmersiveDarkMode, &one, sizeof(one));
      }
      if (roundCorners) {
        setCornerPreference(
            hwnd, roundCornersType == QLatin1String("normal") ? kCornerRound : kCornerRoundSmall,
            borderColor);
      }
    } else {
      setAccentPolicy(hwnd, kAccentAcrylicBlurBehind, 0x01202020);
    }
  } catch (const std::exception& e) {
    qDebug() << "Failed to apply blur:" << e.what();
  }
}

void enableMica(quintptr hwndValue) {
  const HWND hwnd = toHwnd(hwndValue);
  if (!IsWindow(hwnd)) return;
  try {
    const MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (wabar::kernel::windowsBuild() >= 22621) {
      const int v = kBackdropMainWindow;
      DwmSetWindowAttribute(hwnd, kSystemBackdropType, &v, sizeof(v));
    } else {
      const int v = 1;
      DwmSetWindowAttribute(hwnd, kMicaEffect, &v, sizeof(v));
    }
  } catch (const std::exception& e) {
    qDebug() << "Failed to apply mica:" << e.what();
  }
}

void enableDwmFrame(quintptr hwndValue) {
  const HWND hwnd = toHwnd(hwndValue);
  if (!IsWindow(hwnd)) return;
  try {
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (wabar::kernel::windowsBuild() >= 22000)
      setCornerPreference(hwnd, kCornerRound, QStringLiteral("System"));
  } catch (const std::exception& e) {
    qDebug() << "Failed to apply DWM frame:" << e.what();
  }
}

}  // namespace wabar::host

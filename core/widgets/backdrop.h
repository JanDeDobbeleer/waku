#pragma once

#include <QString>
#include <cstdint>

namespace yasb::widgets {

// "#RRGGBBAA" -> AABBGGRR as DwmSetWindowAttribute expects. Throws std::invalid_argument on bad
// input.
std::uint32_t hexToRgbaInt(const QString& hex);

// Windows build number (e.g. 22631), 0 if it cannot be read.
unsigned windowsBuild();
bool isMicaSupported();

// Ports of core/utils/win32/backdrop.py. `hwnd` is QWidget::winId(). Failures are logged, never
// thrown.
void enableBlur(quintptr hwnd, bool darkMode = false, bool roundCorners = false,
                const QString& roundCornersType = QStringLiteral("normal"),
                const QString& borderColor = QStringLiteral("System"));
void enableMica(quintptr hwnd);
void enableDwmFrame(quintptr hwnd);

}  // namespace yasb::widgets

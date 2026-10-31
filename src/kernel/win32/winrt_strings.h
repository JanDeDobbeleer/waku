#pragma once

#include <winrt/base.h>

#include <QString>
#include <cstdint>

namespace wabar::kernel {

// The whole hstring, embedded NULs included (its size, not the first NUL, ends it).
inline QString toQString(const winrt::hstring& text) {
  return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

inline winrt::hstring toHString(const QString& text) {
  return winrt::hstring(reinterpret_cast<const wchar_t*>(text.utf16()),
                        static_cast<std::uint32_t>(text.size()));
}

}  // namespace wabar::kernel

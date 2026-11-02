#pragma once

#include <windows.h>
#include <combaseapi.h>

#include <QString>

namespace waku::audio {

// The text of a CoTaskMemAlloc'd string that Core Audio hands over; frees it. Null gives "".
inline QString takeCoString(LPWSTR text) {
  const QString result = text ? QString::fromWCharArray(text) : QString();
  if (text) CoTaskMemFree(text);
  return result;
}

}  // namespace waku::audio

#include "kernel/win32/environment.h"

#include <windows.h>

#include <vector>

namespace waku::kernel {

QString expandEnvironment(const QString& text) {
  if (!text.contains(QLatin1Char('%'))) return text;
  const auto source = reinterpret_cast<PCWSTR>(text.utf16());
  const DWORD needed = ExpandEnvironmentStringsW(source, nullptr, 0);
  if (needed == 0) return text;
  std::vector<wchar_t> buffer(needed);
  if (ExpandEnvironmentStringsW(source, buffer.data(), needed) == 0) return text;
  return QString::fromWCharArray(buffer.data());
}

}  // namespace waku::kernel

#include "control/win32/pipe_discovery.h"

#include <windows.h>

namespace waku::control {

QStringList findPipesWithPrefix(const QString& prefix, QString* error) {
  error->clear();
  QStringList names;
  const QString pattern = QStringLiteral("\\\\.\\pipe\\") + prefix + QLatin1Char('*');
  WIN32_FIND_DATAW data{};
  HANDLE search = FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()), &data);
  if (search == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    if (code != ERROR_FILE_NOT_FOUND) {
      *error = QStringLiteral("cannot enumerate bar pipes (Windows error %1)").arg(code);
    }
    return names;
  }
  do {
    const QString name = QString::fromWCharArray(data.cFileName);
    const QString suffix = name.mid(prefix.size());
    if (!name.startsWith(prefix) || suffix.size() != 12) continue;
    bool hex = true;
    for (QChar c : suffix) {
      if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
            (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) {
        hex = false;
        break;
      }
    }
    if (hex && !names.contains(name)) names.append(name);
  } while (FindNextFileW(search, &data));
  const DWORD code = GetLastError();
  FindClose(search);
  if (code != ERROR_NO_MORE_FILES) {
    *error = QStringLiteral("cannot enumerate bar pipes (Windows error %1)").arg(code);
    return {};
  }
  names.sort();
  return names;
}

}  // namespace waku::control

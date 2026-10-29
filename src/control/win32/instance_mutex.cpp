#include "control/win32/instance_mutex.h"

#include <windows.h>

#include <string>

namespace wabar {

void* acquireInstanceMutex(const QString& name) {
  const std::wstring mutexName = L"Local\\" + name.toStdWString();
  HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
  if (!mutex) return nullptr;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);  // another instance holds it
    return nullptr;
  }
  return mutex;
}

void releaseInstanceMutex(void* mutex) {
  if (mutex) CloseHandle(static_cast<HANDLE>(mutex));
}

}  // namespace wabar

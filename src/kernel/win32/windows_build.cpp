#include "kernel/win32/windows_build.h"

#include <windows.h>

namespace waku::kernel {

unsigned windowsBuild() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  static const unsigned build = [] {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto fn =
        ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    return fn && fn(&info) == 0 ? static_cast<unsigned>(info.dwBuildNumber) : 0u;
  }();
  return build;
}

}  // namespace waku::kernel

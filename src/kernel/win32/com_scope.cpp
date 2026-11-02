#include "kernel/win32/com_scope.h"

#include <windows.h>
#include <objbase.h>

namespace waku::kernel {

ComScope::ComScope(ComApartment apartment, bool disableOle1Dde) {
  DWORD flags =
      apartment == ComApartment::MultiThreaded ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED;
  if (disableOle1Dde) flags |= COINIT_DISABLE_OLE1DDE;
  result_ = CoInitializeEx(nullptr, flags);
  initialised_ = SUCCEEDED(result_);
}

ComScope::~ComScope() {
  if (initialised_) CoUninitialize();
}

}  // namespace waku::kernel

#include "platform/winrt_apartment.h"

#include <winrt/base.h>

namespace yasb::platform {

void ensureWinrtApartment() {
  thread_local bool initialized = false;
  if (initialized) return;
  initialized = true;
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (const winrt::hresult_error&) {
    // The thread already has an apartment of the other kind; WinRT still works in it.
  }
}

}  // namespace yasb::platform

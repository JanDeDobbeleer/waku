#pragma once

namespace yasb::platform {

// Joins the calling thread to the multi-threaded WinRT apartment once. Safe to call repeatedly
// and from threads that already have an apartment (the mode mismatch is ignored).
void ensureWinrtApartment();

}  // namespace yasb::platform

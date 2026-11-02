#pragma once

namespace waku::kernel {

// The Windows build number (for example 26300) from ntdll!RtlGetVersion, which GetVersionEx
// replaces for unmanifested apps. Read once and cached; 0 when the lookup fails.
unsigned windowsBuild();

}  // namespace waku::kernel

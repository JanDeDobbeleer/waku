#pragma once

#include <QString>
#include <cstdint>
#include <functional>
#include <optional>

#include "features/tray/tray_types.h"

namespace wabar::tray {

enum class HostArch { Unknown, X64, Arm64 };

// IsWow64Process2: the machine Windows runs on, whatever this process was built for.
HostArch hostArch();
// True for a process running under WOW64 or Prism emulation (an x64 build on an ARM64 host).
bool runningUnderEmulation();

// File name of the tray hook DLL for the host architecture ("WabarTrayHook.dll" on x64,
// "WabarTrayHook_arm64.dll" on ARM64); empty when the architecture is unsupported.
QString trayHookDllName();

// Full path of the executable that owns `hwnd`; empty when it cannot be opened.
QString exePathFromHwnd(std::uint64_t hwnd);

// PID of the shell's explorer.exe (the shell window's process, image path checked against
// %windir%\explorer.exe), retrying for about half a second, or until stopping returns true.
// nullopt when not found.
std::optional<std::uint32_t> explorerPid(const std::function<bool()>& stopping = {});

// True when `moduleName` (a base name, case-insensitive) is loaded in process `pid`.
bool isModuleLoaded(std::uint32_t pid, const QString& moduleName);

// True while the left, right or middle mouse button is physically down (GetAsyncKeyState).
bool anyMouseButtonDown();

// Fills data.iconRgba/iconWidth/iconHeight from the HICON in data.hIcon (CopyIcon + GetDIBits,
// converted by colorIconToRgba, or monochromeIconToRgba when there is no colour bitmap). Returns
// false and sets data.iconError when the handle is stale, unreadable or larger than 256 px.
bool extractIconRgba(TrayIconData& data);

}  // namespace wabar::tray

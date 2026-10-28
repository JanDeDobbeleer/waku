#pragma once

#include <QString>
#include <cstdint>
#include <optional>

#include "platform/systray_types.h"

namespace yasb::platform {

enum class HostArch { Unknown, X64, Arm64 };

// IsWow64Process2: the machine Windows runs on, whatever this process was built for.
HostArch hostArch();
// True for a process running under WOW64 or Prism emulation (an x64 build on an ARM64 host).
bool runningUnderEmulation();

// File name of the tray hook DLL for the host architecture ("YASBNativeTrayHook.dll" on x64,
// "YASBNativeTrayHook_arm64.dll" on ARM64); empty when the architecture is unsupported.
QString trayHookDllName();

// Full path of the executable that owns `hwnd`; empty when it cannot be opened.
QString exePathFromHwnd(std::uint64_t hwnd);

// PID of the shell's explorer.exe (the shell window's process, image path checked against
// %windir%\explorer.exe), retrying for about half a second. nullopt when not found.
std::optional<std::uint32_t> explorerPid();

// True when `moduleName` (a base name, case-insensitive) is loaded in process `pid`.
bool isModuleLoaded(std::uint32_t pid, const QString& moduleName);

// True while the left, right or middle mouse button is physically down (GetAsyncKeyState).
bool anyMouseButtonDown();

// Fills data.iconRgba/iconWidth/iconHeight from the HICON in data.hIcon (CopyIcon + GetDIBits,
// the same conversion hook/trayhook.cpp does inside Explorer). Returns false if the handle is
// stale, monochrome or larger than 256 px.
bool extractIconRgba(TrayIconData& data);

}  // namespace yasb::platform

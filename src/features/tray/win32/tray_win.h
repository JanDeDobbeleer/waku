#pragma once

#include <QString>
#include <cstdint>
#include <functional>
#include <optional>

#include "features/tray/tray_types.h"

namespace waku::tray {

// Full path of the executable that owns `hwnd`; empty when it cannot be opened.
QString exePathFromHwnd(std::uint64_t hwnd);

// PID of the shell's explorer.exe (the shell window's process, image path checked against
// %windir%\explorer.exe), retrying for about half a second, or until stopping returns true.
// nullopt when not found.
std::optional<std::uint32_t> explorerPid(const std::function<bool()>& stopping = {});

// Fills data.iconRgba/iconWidth/iconHeight from the HICON in data.hIcon (CopyIcon + GetDIBits,
// converted by colorIconToRgba, or monochromeIconToRgba when there is no colour bitmap). Returns
// false and sets data.iconError when the handle is stale, unreadable or larger than 256 px.
bool extractIconRgba(TrayIconData& data);

}  // namespace waku::tray

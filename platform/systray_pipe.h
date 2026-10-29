#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <functional>

#include "platform/systray_types.h"

namespace yasb::platform {

// Wire format of the tray hook pipe (hook/trayhook.cpp, packed 1):
//   u32 type            1 = text, 2 = WM_COPYDATA copy
//   type 1: UTF-8 text follows
//   type 2: u64 dwData, u32 cbData, u32 iconWidth, u32 iconHeight, u32 iconDataSize,
//           then cbData bytes of SHELLTRAYDATA, then iconDataSize bytes of RGBA8888 pixels.
inline constexpr int kPipeHeaderSize = 28;
// SHELLTRAYDATA as the shell sends it: u32 signature, u32 message, 32-bit NOTIFYICONDATA (956).
inline constexpr int kShellTrayDataSize = 964;

struct ParsedTrayMessage {
  enum class Kind { Invalid, Ignored, Text, IconModified, IconDeleted };
  Kind kind = Kind::Invalid;
  QString text;           // Kind::Text
  TrayIconData data;      // IconModified (validated) and IconDeleted (hWnd, uID, guid only)
  QString invalidReason;  // Kind::Invalid
  // Kind::Invalid only: Python's _process_message raised (ctypes/PIL error), which ends the pipe
  // session. Short messages Python returns from silently are Invalid but not fatal.
  bool fatal = false;
};

// Resolves the executable behind a window; null function means "do not resolve".
using ExePathResolver = std::function<QString(std::uint64_t hwnd)>;

// Port of SystrayHook._process_message plus validate_icon_data. Never throws; a malformed message
// gives Kind::Invalid.
ParsedTrayMessage parseTrayPipeMessage(const QByteArray& message,
                                       const ExePathResolver& exePath = {});

// The SHELLTRAYDATA behind WM_COPYDATA (dwData 1) as Explorer sends it to Shell_TrayWnd.
ParsedTrayMessage parseShellTrayData(const char* payload, std::int64_t size,
                                     const char* rgba = nullptr, std::int64_t rgbaSize = 0,
                                     int iconWidth = 0, int iconHeight = 0,
                                     const ExePathResolver& exePath = {});

// array_to_str: UTF-16 code units up to the first NUL, with CR removed.
QString trayArrayToString(const char16_t* units, int count);

// "Path(exe_path).name.split('.')[0]".
QString trayExeName(const QString& exePath);

// pack_i32: two 16-bit values in one 32-bit integer (the high part sign-extended).
std::int32_t packI32(std::int32_t low, std::int32_t high);

}  // namespace yasb::platform

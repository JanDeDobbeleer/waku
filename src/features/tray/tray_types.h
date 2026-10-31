#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <cstdint>
#include <optional>

namespace wabar::tray {

// NOTIFYICONDATA flags, NIM_* messages and notification codes the tray code relies on.
inline constexpr std::uint32_t kNifMessage = 0x1;
inline constexpr std::uint32_t kNifIcon = 0x2;
inline constexpr std::uint32_t kNifTip = 0x4;
inline constexpr std::uint32_t kNifState = 0x8;
inline constexpr std::uint32_t kNifInfo = 0x10;
inline constexpr std::uint32_t kNifGuid = 0x20;
inline constexpr std::uint32_t kNimAdd = 0x0;
inline constexpr std::uint32_t kNimModify = 0x1;
inline constexpr std::uint32_t kNimDelete = 0x2;
inline constexpr std::uint32_t kNimSetVersion = 0x4;
inline constexpr std::uint32_t kNinSelect = 0x400;
inline constexpr std::uint32_t kNinContextMenu = 0x7B;
inline constexpr std::uint32_t kNisHidden = 0x1;

// Windows' own tray icons (core/utils/win32/undocumented.py).
inline const QUuid kTrayBatteryIconGuid{QStringLiteral("{7820ae75-23e3-4229-82c1-e41cb67d5b9c}")};
inline const QUuid kTrayVolumeIconGuid{QStringLiteral("{7820ae73-23e3-4229-82c1-e41cb67d5b9c}")};
inline const QUuid kTrayNetworkIconGuid{QStringLiteral("{7820ae74-23e3-4229-82c1-e41cb67d5b9c}")};

// Port of the IconData dataclass in services/systray/utils.py. The icon image stays raw RGBA8888
// (non-premultiplied) so this header needs no QtGui; the widget turns it into a QImage.
struct TrayIconData {
  std::uint32_t messageType = 0;
  std::uint64_t hWnd = 0;
  std::uint32_t uID = 0;
  std::optional<QUuid> guid;
  std::uint32_t uFlags = 0;
  std::uint32_t dwState = 0;
  std::uint32_t dwStateMask = 0;
  std::uint64_t hIcon = 0;
  QString szTip;
  QString szInfo;
  QString szInfoTitle;
  std::uint32_t dwInfoFlags = 0;
  std::uint32_t uTimeout = 0;
  std::uint32_t uCallbackMessage = 0;
  std::uint32_t uVersion = 0;
  int iconWidth = 0;
  int iconHeight = 0;
  QByteArray iconRgba;
  // iconRgba already scaled to the 32x32 tray size (scaleTrayIcon, RGBA8888), when the host did it
  // on its own thread; empty otherwise (a 32x32 icon needs no scaling). Travels with iconRgba.
  QByteArray scaledRgba;
  // Set by the host for one message when NIF_ICON came without readable pixels: why the handle
  // could not be read. Diagnostics only; never stored with the icon.
  QString iconError;
  QString exe;
  QString exePath;
};

}  // namespace wabar::tray

Q_DECLARE_METATYPE(wabar::tray::TrayIconData)

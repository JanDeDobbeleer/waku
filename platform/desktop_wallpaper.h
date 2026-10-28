#pragma once

#include <QRect>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace yasb::platform {

// One monitor as IDesktopWallpaper knows it: the device interface path and its rectangle in
// physical desktop pixels.
struct WallpaperMonitor {
  QString id;
  QRect nativeRect;
};

// The wallpaper side of IDesktopWallpaper (shobjidl_core.h, documented, Windows 8+). The widget
// only talks to this interface, so tests substitute a fake and never touch the real wallpaper.
// Calls may come from any thread; the real backend initialises COM per call.
class DesktopWallpaperBackend {
 public:
  virtual ~DesktopWallpaperBackend() = default;

  // An empty `monitorId` sets every monitor. Returns false and fills `error` on failure.
  virtual bool setWallpaper(const QString& monitorId, const QString& path, QString* error) = 0;
  virtual std::vector<WallpaperMonitor> monitors() = 0;
  // The wallpaper file shown on a monitor, empty if unknown.
  virtual QString wallpaperOf(const QString& monitorId) = 0;
};

// The COM backend (CLSID_DesktopWallpaper).
std::unique_ptr<DesktopWallpaperBackend> makeDesktopWallpaperBackend();

// Windows build number and UBR (HKLM CurrentVersion); UBR is 0 when unreadable, build 0 if the
// registry cannot be read.
struct WindowsBuild {
  int build = 0;
  int ubr = 0;
};
WindowsBuild windowsBuildAndUbr();

// shlwapi StrCmpLogicalW: "img2" sorts before "img10" the way Explorer orders names.
int compareLogical(const QString& a, const QString& b);

}  // namespace yasb::platform

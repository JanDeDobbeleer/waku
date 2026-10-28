#pragma once

#include <QRect>
#include <QString>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace yasb::platform {

// The desktop wallpaper settings the wallpaper transition mirrors (HKCU\Control Panel\Desktop and
// HKCU\Control Panel\Colors). Every reader falls back to Windows' own default.
enum class WallpaperFit { Fill, Fit, Stretch, Tile, Center, Span };
WallpaperFit readWallpaperFit();
// 0x00RRGGBB, black when unreadable.
std::uint32_t readDesktopBackgroundRgb();
// MaxVirtualDesktopDimension and MaxMonitorDimension; nullopt when either is missing.
std::optional<std::pair<int, int>> readTranscodeLimits();
// PanoramaThreshold / PanoramaPortraitThreshold in thousandths, defaults 2.22 / 1.0.
double readPanoramaThreshold(bool landscape);
// %APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper.
QString transcodedWallpaperPath();

// Where a top-level window ended up after being parented to WorkerW, in physical pixels.
struct WorkerWAttachment {
  QRect workerRect;
  std::vector<QRect> monitors;
};

// Finds (spawning if needed) the WorkerW window behind the desktop icons, reparents `hwnd` to it
// as a child and sizes it to cover WorkerW. Returns nullopt with `failure` set when Progman or
// WorkerW cannot be found.
std::optional<WorkerWAttachment> attachToWorkerW(std::uintptr_t hwnd, QString* failure);

}  // namespace yasb::platform

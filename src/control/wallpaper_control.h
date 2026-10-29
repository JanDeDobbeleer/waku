#pragma once

#include <QString>
#include <functional>

namespace wabar::control {

// What the `wallpaper` commands of `wabarc` ask of the wallpaper service. The control handler
// depends on this interface only (so its tests use a fake); wabar::services::Wallpapers
// implements it. Every function returns the answer line: "ok: <text>" or "error: <text>".
// `dryRun` plays the transition but never calls the wallpaper setter.
class WallpaperControl {
 public:
  virtual ~WallpaperControl() = default;
  // `target` is a full path to an image or a file name inside the wallpaper folder.
  virtual QString controlSet(const QString& target, bool dryRun) = 0;
  virtual QString controlRandom(bool dryRun) = 0;
  virtual QString controlNext(bool dryRun) = 0;
  virtual QString controlPrevious(bool dryRun) = 0;
  // Toggles the picker.
  virtual QString controlGallery() = 0;
};

// Looked up when a command arrives; null when there is no wallpaper service.
using WallpaperControlProvider = std::function<WallpaperControl*()>;

}  // namespace wabar::control

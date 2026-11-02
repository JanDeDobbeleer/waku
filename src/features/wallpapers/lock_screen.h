#pragma once

#include <QString>
#include <memory>
#include <optional>

namespace waku::wallpapers {

// Sets the Windows lock screen picture, which the sign-in screen shows too. The wallpaper service
// only talks to this interface, so tests substitute a fake and never touch the real lock screen.
class LockScreenSetter {
 public:
  virtual ~LockScreenSetter() = default;

  // Queues `path` (an absolute path to an existing still image) and returns at once. Only the
  // latest queued path is set. A failure is logged as one warning line; nothing throws.
  virtual void set(const QString& path) = 0;
};

// The real setter (win32/lock_screen_win.cpp): WinRT LockScreen.SetImageFileAsync (documented,
// Windows.System.UserProfile) on a worker thread of its own. `cacheDir` takes the PNG copy of an
// image Windows does not accept as is.
std::unique_ptr<LockScreenSetter> makeLockScreenSetter(const QString& cacheDir);

// Why `path` cannot become the lock screen picture: a missing file, a video or an animated image.
// std::nullopt means it can (perhaps after lockScreenNeedsCopy()).
std::optional<QString> lockScreenSkipReason(const QString& path);

// True when Windows does not take the file as is (anything but jpg, jpeg, png and bmp), so the
// setter writes a PNG copy first.
bool lockScreenNeedsCopy(const QString& path);

}  // namespace waku::wallpapers

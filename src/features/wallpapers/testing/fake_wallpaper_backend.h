// A fake of the wallpaper service's desktop: it remembers what it was told to show. Nothing
// touches the real wallpaper. The hotkey's fake is the kernel's
// (kernel/testing/hotkeys_fixtures.h).
#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "features/wallpapers/desktop_wallpaper.h"

namespace wabar::wallpapers {

// What the fake desktop was told, and how it answers. Shared by the backends a test creates.
struct DesktopState {
  struct Call {
    QString monitor;  // empty: every monitor
    QString path;
  };
  QString wallpaper;        // the last path set
  QStringList sets;         // every path set, in order
  std::vector<Call> calls;  // every set with its monitor id, in order
  std::vector<WallpaperMonitor> monitors{{QStringLiteral("mon-1"), QRect(0, 0, 100, 100)}};
  bool failSet = false;  // setWallpaper fails and changes nothing
};

class FakeBackend final : public DesktopWallpaperBackend {
 public:
  explicit FakeBackend(std::shared_ptr<DesktopState> state) : state_(std::move(state)) {}
  bool setWallpaper(const QString& monitorId, const QString& path, QString* error) override {
    if (state_->failSet) {
      if (error) *error = QStringLiteral("fake failure");
      return false;
    }
    state_->wallpaper = path;
    state_->sets << path;
    state_->calls.push_back({monitorId, path});
    return true;
  }
  std::vector<WallpaperMonitor> monitors() override { return state_->monitors; }
  QString wallpaperOf(const QString&) override { return state_->wallpaper; }

 private:
  std::shared_ptr<DesktopState> state_;
};

}  // namespace wabar::wallpapers

// Fakes of the wallpaper service's two OS-facing interfaces: a desktop that remembers what it was
// told to show, and a hotkey host the test fires by hand. Nothing touches the real wallpaper or
// registers a real hotkey.
#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "features/wallpapers/desktop_wallpaper.h"
#include "features/wallpapers/wallpaper_hotkey.h"

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

class FakeHotkeys final : public HotkeyHost {
 public:
  bool registerKey(const HotkeyCombo& combo, std::function<void()> pressed) override {
    attempts.push_back(combo);
    if (!succeed) return false;
    callback = std::move(pressed);
    return true;
  }
  void unregisterKey() override {
    ++unregisters;
    callback = nullptr;
  }
  bool succeed = true;
  std::vector<HotkeyCombo> attempts;
  int unregisters = 0;
  std::function<void()> callback;
};

}  // namespace wabar::wallpapers

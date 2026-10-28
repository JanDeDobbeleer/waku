#pragma once

#include <QPointer>
#include <QScreen>
#include <functional>
#include <memory>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "widgets/wallpapers/gallery.h"
#include "widgets/wallpapers/wallpaper_manager.h"

namespace yasb::widgets {

// Port of core/widgets/yasb/wallpapers.py (WallpapersWidget): an (often empty-labelled) bar item
// that opens the wallpaper gallery (toggle_gallery, also bound to a hotkey) or switches to a
// random wallpaper (change_wallpaper). The manager is shared by all wallpapers widgets.
//
// Thread: GUI thread only; the gallery owns its own pool (see WallpaperGallery).
class WallpapersWidget final : public BaseWidget {
  Q_OBJECT
 public:
  // Test seams; null members mean the real thing.
  struct Hooks {
    WallpaperManager* manager = nullptr;
    GalleryScanner scanner;
    std::shared_ptr<ThumbnailCache> cache;
    // Screen to open the gallery on for a keybinding mode ("active", "cursor", "primary").
    std::function<QScreen*(const QString& mode)> screenForMode;
  };

  explicit WallpapersWidget(const WidgetContext& context, Hooks hooks = {});
  ~WallpapersWidget() override;

  void toggleGallery();
  WallpaperGallery* gallery() const { return gallery_; }
  WallpaperManager* manager() const { return manager_; }
  // The screen the gallery opens on, per the toggle_gallery keybinding (default "active").
  QScreen* targetScreen() const;

 private:
  config::options::WallpapersConfig options_;
  Hooks hooks_;
  WallpaperManager* manager_ = nullptr;
  QPointer<WallpaperGallery> gallery_;
};

void registerWallpaperWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets

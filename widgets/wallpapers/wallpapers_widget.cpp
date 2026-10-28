#include "widgets/wallpapers/wallpapers_widget.h"

#include <QCursor>
#include <QGuiApplication>
#include <stdexcept>

#include "core/config/paths.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/tooltip.h"
#include "platform/accent_color.h"
#include "platform/window_focus.h"
#include "widgets/wallpapers/wallpaper_files.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

// find_focused_screen for the keybinding modes: "cursor" follows the mouse, "active" the focused
// window; both fall back to the primary screen.
QScreen* screenForModeSystem(const QString& mode) {
  if (mode == QLatin1String("primary")) return QGuiApplication::primaryScreen();
  if (mode == QLatin1String("cursor")) {
    const QPoint position = QCursor::pos();
    for (QScreen* screen : QGuiApplication::screens()) {
      if (screen->geometry().contains(position)) return screen;
    }
  } else if (mode == QLatin1String("active")) {
    if (const auto monitor = platform::foregroundMonitorRect()) {
      // Qt keeps a screen's top-left in desktop pixels, so it identifies the monitor.
      for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->geometry().topLeft() == monitor->topLeft()) return screen;
      }
    }
  }
  return QGuiApplication::primaryScreen();
}

std::shared_ptr<ThumbnailCache> defaultCache() {
  const std::filesystem::path folder = config::dataDir() / "wallpaper_thumbnails";
  return std::make_shared<ThumbnailCache>(QString::fromStdWString(folder.wstring()));
}

}  // namespace

WallpapersWidget::WallpapersWidget(const WidgetContext& context, Hooks hooks)
    : BaseWidget(qs(context.name), context.eventBus, QStringLiteral("wallpapers-widget"), 0),
      options_(std::get<config::options::WallpapersConfig>(context.typedOptions)),
      hooks_(std::move(hooks)) {
  manager_ = hooks_.manager;
  if (!manager_) {
    manager_ = context.pollingWorkers.service<WallpaperManager>(
        "wallpapers", [] { return new WallpaperManager(); });
  }
  QStringList runAfter;
  for (const std::string& command : options_.run_after) runAfter << qs(command);
  WallpaperManager::EngineOptions engine;
  engine.enabled = options_.engine.enabled;
  if (!options_.engine.animation.empty()) engine.animation = qs(options_.engine.animation);
  manager_->configure(imagePathsFrom(options_.image_path), options_.update_interval,
                      options_.change_automatically, runAfter, engine);

  buildWidgetLabel(qs(options_.label));
  if (options_.tooltip) setTooltip(this, QStringLiteral("Change Wallpaper"), context.globalState);

  registerCallback(QStringLiteral("toggle_gallery"),
                   [this](const QStringList&) { toggleGallery(); });
  registerCallback(
      QStringLiteral("change_wallpaper"),
      [this](const QStringList& args) {
        if (args.size() > 1)
          throw std::invalid_argument("change_wallpaper takes at most one image");
        manager_->changeBackground(args.isEmpty() ? QString() : args.first());
      },
      -1);
  setCallbackBinding({qs(options_.callbacks.on_left), qs(options_.callbacks.on_middle),
                      qs(options_.callbacks.on_right), QStringLiteral("do_nothing")});
}

WallpapersWidget::~WallpapersWidget() {
  // An open gallery would outlive the bar that opened it.
  delete gallery_.data();
}

QScreen* WallpapersWidget::targetScreen() const {
  QString mode = QStringLiteral("active");
  for (const auto& binding : options_.keybindings) {
    if (binding.action == "toggle_gallery") {
      mode = qs(binding.screen);
      break;
    }
  }
  return hooks_.screenForMode ? hooks_.screenForMode(mode) : screenForModeSystem(mode);
}

void WallpapersWidget::toggleGallery() {
  if (gallery_ && gallery_->isVisible()) {
    gallery_->fadeOutAndClose();
    return;
  }

  WallpaperGallery::Options options;
  options.imagePaths = imagePathsFrom(options_.image_path);
  options.type = qs(options_.gallery.type);
  options.imageWidth = options_.gallery.image_width;
  options.orientation = qs(options_.gallery.orientation);
  options.cornerRadius = options_.gallery.image_corner_radius;
  options.accentColor = qs(options_.gallery.accent_color);
  if (const QWidget* bar = window()) options.styleSheet = bar->styleSheet();

  WallpaperGallery::Services services;
  services.manager = manager_;
  services.scanner =
      hooks_.scanner ? hooks_.scanner : makeSystemScanner(manager_->backendFactory());
  services.cache = hooks_.cache ? hooks_.cache : defaultCache();
  services.systemAccent = [] {
    const auto rgb = platform::systemAccentRgb();
    return rgb ? QColor((*rgb >> 16) & 0xFF, (*rgb >> 8) & 0xFF, *rgb & 0xFF) : QColor();
  };

  auto* gallery = new WallpaperGallery(std::move(options), std::move(services));
  gallery_ = gallery;
  gallery->fadeIn(this, targetScreen());
}

void registerWallpaperWidgets(WidgetRegistry& registry) {
  registry.add("yasb.wallpapers.WallpapersWidget",
               [](const WidgetContext& context) { return new WallpapersWidget(context); });
}

}  // namespace yasb::widgets

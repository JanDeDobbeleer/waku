#pragma once

#include <QtQml/qqmlregistration.h>

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QVariantList>
#include <functional>
#include <memory>
#include <optional>

#include "features/wallpapers/desktop_wallpaper.h"
#include "features/wallpapers/qml/wallpaper_transition.h"
#include "features/wallpapers/thumbnail_cache.h"
#include "features/wallpapers/wallpaper_hotkey.h"
#include "features/wallpapers/wallpaper_manager.h"

// The control interface headers are a stable shared abstraction: a feature may include
// control/ (rule S2) to implement the interface the bar's handler calls.
#include "control/wallpaper_control.h"

class QJSEngine;
class QQmlEngine;

namespace wabar::wallpapers {

// The file a `wabarc wallpaper set` target means. `target` is a full path (any readable image
// file) or a file name inside `folder` (case-insensitive; without an extension the supported
// ones are tried). On success `path` is the full path; otherwise `error` says why (no "error: "
// prefix). Checks that the file exists, is a regular file, has a supported image extension and
// a header Qt can read.
struct ResolvedWallpaper {
  QString path;
  QString error;
  bool ok() const { return error.isEmpty(); }
};
ResolvedWallpaper resolveWallpaperTarget(const QString& target, const QString& folder);

// The wallpaper folder, the current wallpaper and the gallery of every Wallpapers widget of a
// `wabar` run, as a QML singleton. Wraps the UI-free classes of the Widgets build:
// wabar::wallpapers::WallpaperManager (set, random, auto-change timer; the animated change is the
// Qt Quick WallpaperTransition, see docs/plans/2026-10-29-wabar-wallpapers-design.md),
// collectImageFiles, ThumbnailCache and DesktopWallpaperBackend. Registers
// image://wallpaper/<path>?<stamp>.
//
// Threads: GUI thread, no thread of its own (the transition's decoder thread is described there).
// The thumbnail provider runs on Qt's image-reader thread (ForceAsynchronousImageLoading); it owns
// a shared ThumbnailCache and reads image files, nothing else. The hotkey arrives on the GUI
// thread.
//
// Several `Wallpapers` widgets and a `WallpaperSettings` share this one service; the last one
// to call configure()/configureEngine() wins.
//
// `files` is a list of maps: path, name, thumbnail (image URL) and current (bool), in Explorer
// name order. `available` is false (nothing is ever set) when IDesktopWallpaper cannot be
// created: not a live desktop (kernel::liveDesktop("wallpapers")). The hotkey is never registered
// then.
class Wallpapers : public QObject, public wabar::control::WallpaperControl {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY imagePathChanged FINAL)
  Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged FINAL)
  Q_PROPERTY(QString current READ current NOTIFY currentChanged FINAL)
  Q_PROPERTY(bool galleryOpen READ galleryOpen NOTIFY galleryOpenChanged FINAL)
  Q_PROPERTY(QString galleryScreen READ galleryScreen NOTIFY galleryOpenChanged FINAL)

 public:
  // Everything a test replaces. Null members are the real ones (cache: the shared folder under
  // %LOCALAPPDATA%\wabar); `realBackend=false` with a null `backend` means unavailable.
  struct Deps {
    wabar::wallpapers::WallpaperManager::BackendFactory backend;
    wabar::wallpapers::WallpaperManager::Picker picker;
    std::shared_ptr<wabar::wallpapers::ThumbnailCache> cache;
    std::unique_ptr<HotkeyHost> hotkeys;
    // Builds the transition for (image, animation); null means the real WallpaperTransition.
    std::function<std::unique_ptr<WallpaperTransition>(const QString&, const QString&)> transition;
    bool realBackend = true;
    // Play the transition but never call SetWallpaper (WABAR_WALLPAPER_DRYRUN=1 sets it). A
    // `wabarc ... --dry-run` does the same for one call.
    bool dryRun = false;
  };

  // Deliberately not default-constructible: Qt uses a singleton's default constructor in
  // preference to create(), and create() must run to register the image provider.
  explicit Wallpapers(QObject* parent);
  explicit Wallpapers(Deps deps, QObject* parent = nullptr);
  ~Wallpapers() override;

  static Wallpapers* create(QQmlEngine* engine, QJSEngine*);
  // Tests: create() builds the singleton with this factory instead of the real one.
  static void setFactoryForTests(std::function<std::unique_ptr<Wallpapers>()> factory);

  bool available() const { return available_; }
  QString imagePath() const { return imagePath_; }
  void setImagePath(const QString& path);
  QVariantList files() const { return files_; }
  QString current() const { return current_; }
  bool galleryOpen() const { return galleryOpen_; }
  QString galleryScreen() const { return galleryScreen_; }
  const std::shared_ptr<wabar::wallpapers::ThumbnailCache>& cache() const { return cache_; }

  // What Wallpapers.qml passes in: the folder, the toggle hotkey ("" for none), the screen mode
  // for it (cursor, primary, active; anything else is primary) and the auto-change interval in
  // seconds (0 off). Safe to call again from every bar; the last call wins.
  Q_INVOKABLE void configure(const QString& imagePath, const QString& hotkey,
                             const QString& hotkeyScreen, int autoChangeSeconds);
  // The animated change: `enabled` plays the transition (Qt Quick overlay on WorkerW) before the
  // wallpaper is set, `animation` is circle, slide_top, diamond, split or fade. Off by default.
  Q_INVOKABLE void configureEngine(bool enabled, const QString& animation);
  // Rescans the folder and reads the current wallpaper.
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void setWallpaper(const QString& path);
  Q_INVOKABLE void random();
  Q_INVOKABLE void toggleGallery();
  Q_INVOKABLE void closeGallery();
  // `wabarc wallpaper ...` (WallpaperControl): the answer is "ok: <full path>" once the change
  // has started (the animation may still run), or "error: <why>" with nothing changed. `dryRun`
  // plays the animation and skips the setter, for this call only.
  QString controlSet(const QString& target, bool dryRun) override;
  QString controlRandom(bool dryRun) override;
  QString controlNext(bool dryRun) override;
  QString controlPrevious(bool dryRun) override;
  QString controlGallery() override;
  // The hotkey was pressed (public for tests).
  void onHotkey() { toggleGallery(); }

 signals:
  void imagePathChanged();
  void filesChanged();
  void currentChanged();
  void galleryOpenChanged();

 private:
  QString screenNameFor(const QString& mode) const;
  void readCurrent();
  void applyToManager();
  void commitWallpaper(const QString& path, bool dryRun);
  void playTransition(const QString& path, bool dryRun);
  void setWallpaperImpl(const QString& path, bool dryRun);
  QString controlStep(int step, bool dryRun);
  QString controlStart(const QString& path, bool dryRun);

  Deps deps_;
  bool available_ = false;
  std::shared_ptr<wabar::wallpapers::ThumbnailCache> cache_;
  std::unique_ptr<DesktopWallpaperBackend> backend_;
  std::unique_ptr<wabar::wallpapers::WallpaperManager> manager_;
  QString imagePath_;
  QString hotkey_;
  int autoChangeSeconds_ = 0;
  QString hotkeyScreen_ = QStringLiteral("cursor");
  QVariantList files_;
  QString current_;
  bool galleryOpen_ = false;
  QString galleryScreen_;
  bool engineEnabled_ = false;
  QString animation_ = QStringLiteral("split");
  std::unique_ptr<WallpaperTransition> transition_;
  bool skipLogged_ = false;
};

// image://wallpaper/<path>?<stamp>: the thumbnail of one file, scaled to fit `requestedSize`
// (the QML sourceSize) and kept in the shared ThumbnailCache. Holds only the cache.
class WallpaperThumbnailProvider : public QQuickImageProvider {
 public:
  explicit WallpaperThumbnailProvider(std::shared_ptr<wabar::wallpapers::ThumbnailCache> cache);

  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

 private:
  std::shared_ptr<wabar::wallpapers::ThumbnailCache> cache_;
};

}  // namespace wabar::wallpapers

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

#include "platform/desktop_wallpaper.h"
#include "widgets/wallpapers/thumbnail_cache.h"
#include "widgets/wallpapers/wallpaper_manager.h"

class QJSEngine;
class QQmlEngine;

namespace wabar::services {

namespace platform = ::yasb::platform;

// A global hotkey as RegisterHotKey wants it: MOD_ALT 1, MOD_CONTROL 2, MOD_SHIFT 4, MOD_WIN 8.
struct HotkeyCombo {
  unsigned modifiers = 0;
  unsigned virtualKey = 0;
};
// "alt+e", "ctrl+shift+f5", "win+space": modifiers alt/ctrl/control/shift/win/windows/super/meta
// plus one letter, digit, F1..F24 or space. Case and spaces do not matter. nullopt otherwise.
std::optional<HotkeyCombo> parseHotkey(const QString& keys);

// Where the hotkey lives. The real one calls RegisterHotKey on the GUI thread and receives
// WM_HOTKEY through a native event filter in Qt's own message loop; tests replace it.
class HotkeyHost {
 public:
  virtual ~HotkeyHost() = default;
  // False when the combination is taken or the call fails (the caller logs and carries on).
  virtual bool registerKey(const HotkeyCombo& combo, std::function<void()> pressed) = 0;
  virtual void unregisterKey() = 0;
};

// The wallpaper folder, the current wallpaper and the gallery of every Wallpapers widget of a
// `wabar` run, as a QML singleton. Wraps the UI-free classes of the Widgets build:
// yasb::widgets::WallpaperManager (set, random, auto-change timer; the animated `engine` is off,
// see docs/plans/2026-10-29-wabar-wallpapers-design.md), collectImageFiles, ThumbnailCache and
// platform::DesktopWallpaperBackend. Registers image://wallpaper/<path>?<stamp>.
//
// Threads: GUI thread, no thread of its own. The thumbnail provider runs on Qt's image-reader
// thread (ForceAsynchronousImageLoading); it owns a shared ThumbnailCache and reads image files,
// nothing else. The hotkey arrives on the GUI thread.
//
// `files` is a list of maps: path, name, thumbnail (image URL) and current (bool), in Explorer
// name order. `available` is false (nothing is ever set) when IDesktopWallpaper cannot be
// created: the offscreen platform and setDisabled(true). The hotkey is never registered then.
class Wallpapers : public QObject {
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
  // %LOCALAPPDATA%\YASB); `realBackend=false` with a null `backend` means unavailable.
  struct Deps {
    yasb::widgets::WallpaperManager::BackendFactory backend;
    yasb::widgets::WallpaperManager::Picker picker;
    std::shared_ptr<yasb::widgets::ThumbnailCache> cache;
    std::unique_ptr<HotkeyHost> hotkeys;
    bool realBackend = true;
  };

  // Deliberately not default-constructible: Qt uses a singleton's default constructor in
  // preference to create(), and create() must run to register the image provider.
  explicit Wallpapers(QObject* parent);
  explicit Wallpapers(Deps deps, QObject* parent = nullptr);
  ~Wallpapers() override;

  static Wallpapers* create(QQmlEngine* engine, QJSEngine*);
  // Tests: create() builds the singleton with this factory instead of the real one.
  static void setFactoryForTests(std::function<std::unique_ptr<Wallpapers>()> factory);
  // `wabar --no-wallpapers` (once wired): no IDesktopWallpaper, no hotkey.
  static void setDisabled(bool disabled);

  bool available() const { return available_; }
  QString imagePath() const { return imagePath_; }
  void setImagePath(const QString& path);
  QVariantList files() const { return files_; }
  QString current() const { return current_; }
  bool galleryOpen() const { return galleryOpen_; }
  QString galleryScreen() const { return galleryScreen_; }
  const std::shared_ptr<yasb::widgets::ThumbnailCache>& cache() const { return cache_; }

  // What Wallpapers.qml passes in: the folder, the toggle hotkey ("" for none), the screen mode
  // for it (cursor, primary, active; anything else is primary) and the auto-change interval in
  // seconds (0 off). Safe to call again from every bar; the last call wins.
  Q_INVOKABLE void configure(const QString& imagePath, const QString& hotkey,
                             const QString& hotkeyScreen, int autoChangeSeconds);
  // Rescans the folder and reads the current wallpaper.
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void setWallpaper(const QString& path);
  Q_INVOKABLE void random();
  Q_INVOKABLE void toggleGallery();
  Q_INVOKABLE void closeGallery();
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

  Deps deps_;
  bool available_ = false;
  std::shared_ptr<yasb::widgets::ThumbnailCache> cache_;
  std::unique_ptr<platform::DesktopWallpaperBackend> backend_;
  std::unique_ptr<yasb::widgets::WallpaperManager> manager_;
  QString imagePath_;
  QString hotkey_;
  int autoChangeSeconds_ = 0;
  QString hotkeyScreen_ = QStringLiteral("cursor");
  QVariantList files_;
  QString current_;
  bool galleryOpen_ = false;
  QString galleryScreen_;
};

// image://wallpaper/<path>?<stamp>: the thumbnail of one file, scaled to fit `requestedSize`
// (the QML sourceSize) and kept in the shared ThumbnailCache. Holds only the cache.
class WallpaperThumbnailProvider : public QQuickImageProvider {
 public:
  explicit WallpaperThumbnailProvider(std::shared_ptr<yasb::widgets::ThumbnailCache> cache);

  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

 private:
  std::shared_ptr<yasb::widgets::ThumbnailCache> cache_;
};

}  // namespace wabar::services

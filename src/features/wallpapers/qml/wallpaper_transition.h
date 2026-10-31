#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>
#include <QVariantAnimation>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "features/wallpapers/wallpaper_layout.h"
#include "features/wallpapers/win32/workerw.h"

class QPainter;
class QQuickWindow;
class QThread;
class QTimer;

namespace wabar::wallpapers {

// Draws one frame of the reveal for every monitor area: the old image, then the new image clipped
// by the animation's shape at progress `t` (0..1). circle, slide_top, diamond and split use
// revealPath; "fade" (also any unknown name) blends the new image in with opacity `t`.
// Free so tests can render a frame to a QImage.
void paintWallpaperFrame(QPainter& painter, const std::vector<ScreenArea>& areas,
                         const std::vector<ScaledImage>& oldImages,
                         const std::vector<ScaledImage>& newImages, double dpr,
                         const QColor& background, const QString& animation, double t);

// The one Qt Quick item of the overlay scene. GUI thread (scene graph painting runs on it for the
// Image render target).
class WallpaperScene : public QQuickPaintedItem {
  Q_OBJECT
 public:
  explicit WallpaperScene(QQuickItem* parent = nullptr);
  void setContent(std::vector<ScreenArea> areas, std::vector<ScaledImage> oldImages,
                  std::vector<ScaledImage> newImages, double dpr, QColor background,
                  QString animation);
  void setProgress(double value);
  double progress() const { return progress_; }
  void paint(QPainter* painter) override;

 private:
  std::vector<ScreenArea> areas_;
  std::vector<ScaledImage> old_;
  std::vector<ScaledImage> new_;
  double dpr_ = 1.0;
  QColor background_ = Qt::black;
  QString animation_;
  double progress_ = 0.0;
};

// Plays the reveal of a new wallpaper over the old one on a frameless QQuickWindow that is
// re-parented to WorkerW (above the wallpaper, below the desktop icons), then emits finished() so
// the caller can hand the image to Windows. Qt Quick port of WallpaperEngine; no Qt
// Widgets. The window lingers kLingerMs after finished() so the desktop never shows the old
// picture before Windows has switched, and is gone on cancel() and on destruction.
//
// Threads: GUI thread. start() runs one short-lived QThread that only decodes the two images
// (Platform::decode); it touches no Qt Quick object and posts its result to the GUI thread through
// a QPointer that is checked there. The thread deletes itself when it ends. cancel() never waits
// for it; the destructor does (at most one decode), so no decoder outlives the application.
//
// When an image cannot be read or WorkerW cannot be found the window never shows: skipped()
// carries the reason, then finished() follows at once.
class WallpaperTransition : public QObject {
  Q_OBJECT
 public:
  // The OS reads, injectable so tests never touch the registry, the real WorkerW or any file.
  struct Platform {
    std::function<DesktopSettings()> readSettings;
    std::function<QString()> oldImagePath;
    std::function<std::optional<WorkerWAttachment>(std::uintptr_t, QString*)> attach;
    // Undoes attach before the window is deleted (null in tests).
    std::function<void(std::uintptr_t)> detach;
    // Runs on the worker thread: the new image, then the old one. Null images mean unreadable.
    std::function<std::pair<QImage, QImage>(const QString&, const QString&)> decode;
    // False in tests: the window is built and filled but never shown.
    bool showWindow = true;
  };
  static Platform systemPlatform();

  static constexpr int kAnimationMs = 1200;
  static constexpr int kLingerMs = 800;

  enum class State { Idle, Decoding, Playing, Finished, Skipped, Cancelled };

  WallpaperTransition(QString imagePath, QString animation, Platform platform = systemPlatform(),
                      int animationMs = kAnimationMs, QObject* parent = nullptr);
  ~WallpaperTransition() override;

  // Decodes both images in the background, then attaches to WorkerW and plays.
  void start();
  // The step after decoding (public so tests can feed images without a thread).
  void onImagesLoaded(const QImage& newImage, const QImage& oldImage);
  // Stops the animation and removes the window without emitting anything.
  void cancel();

  State state() const { return state_; }
  QString animation() const { return animation_; }
  double progress() const { return progress_; }
  void setProgress(double value);
  bool windowVisible() const;
  const std::vector<ScreenArea>& areas() const { return areas_; }
  const std::vector<ScaledImage>& scaledNew() const { return scaledNew_; }
  const std::vector<ScaledImage>& scaledOld() const { return scaledOld_; }
  // Renders the frame at the current progress, in logical pixels of the overlay (tests).
  QImage renderFrame() const;

 signals:
  void finished();
  void skipped(const QString& reason);

 private:
  void skip(const QString& reason);
  void destroyWindow();

  QString imagePath_;
  QString animation_;
  Platform platform_;
  int animationMs_;
  DesktopSettings settings_;
  State state_ = State::Idle;
  double progress_ = 0.0;
  double dpr_ = 1.0;
  QSize size_;
  std::vector<ScreenArea> areas_;
  std::vector<ScaledImage> scaledNew_;
  std::vector<ScaledImage> scaledOld_;
  QQuickWindow* window_ = nullptr;
  bool attached_ = false;  // window_ is a child of WorkerW
  WallpaperScene* scene_ = nullptr;
  QPointer<QThread> decoder_;
  QVariantAnimation* animationTimer_ = nullptr;
  QTimer* lingerTimer_ = nullptr;
};

}  // namespace wabar::wallpapers

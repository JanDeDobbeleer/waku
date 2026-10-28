#pragma once

#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QThread>
#include <QTimeLine>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "platform/workerw.h"
#include "widgets/wallpapers/wallpaper_layout.h"

namespace yasb::widgets {

// Port of services/wallpapers/engine.py: plays a short reveal of the new wallpaper over the old
// one on an overlay parented to WorkerW (above the wallpaper, below the desktop icons), then
// emits finished() so the caller can hand the image to Windows.
//
// Thread: GUI thread. One short-lived QThread decodes the two images; it never touches widgets and
// is joined in the destructor.
//
// When the images cannot be read or WorkerW cannot be found the overlay never shows: skipped()
// carries the reason, finished() follows at once and the engine deletes itself.
class WallpaperEngine : public QWidget {
  Q_OBJECT
 public:
  // The OS reads, injectable so tests never touch the registry or the real WorkerW.
  struct Platform {
    std::function<DesktopSettings()> readSettings;
    std::function<QString()> transcodedPath;
    std::function<std::optional<platform::WorkerWAttachment>(std::uintptr_t, QString*)> attach;
  };
  static Platform systemPlatform();

  static constexpr int kAnimationMs = 1200;
  static constexpr int kFrameMs = 16;
  static constexpr int kFadeInMs = 50;

  WallpaperEngine(const QString& imagePath, const QString& animation,
                  Platform platform = systemPlatform(), int animationMs = kAnimationMs);
  ~WallpaperEngine() override;

  // Decodes both images in the background, then attaches to WorkerW and begins the animation.
  void start();
  // The step after decoding (public so tests can feed images without a thread).
  void onImagesLoaded(const QImage& newImage, const QImage& oldImage);

  // True once the overlay is up and animating.
  bool animated() const { return animated_; }
  double progress() const { return progress_; }
  const std::vector<ScreenArea>& areas() const { return areas_; }
  const std::vector<ScaledImage>& scaledNew() const { return scaledNew_; }
  const std::vector<ScaledImage>& scaledOld() const { return scaledOld_; }
  void setProgress(double value);

 signals:
  void finished();
  void skipped(const QString& reason);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void skip(const QString& reason);
  void setScreenAreas(std::vector<ScreenArea> areas);
  void startFadeIn();
  void onTimelineFinished();

  QString imagePath_;
  QString animation_;
  Platform platform_;
  int animationMs_;
  DesktopSettings settings_;
  double progress_ = 0.0;
  bool committed_ = false;
  bool animated_ = false;
  double dpr_ = 1.0;
  std::vector<ScreenArea> areas_;
  std::vector<ScaledImage> scaledNew_;
  std::vector<ScaledImage> scaledOld_;
  QPixmap pixmapNew_;
  QPixmap pixmapOld_;
  QTimeLine* timeline_ = nullptr;
  QTimeLine* fadeTimeline_ = nullptr;
  QPointer<QThread> loader_;
};

}  // namespace yasb::widgets

#pragma once

#include <QColor>
#include <QImage>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <atomic>
#include <vector>

#include "features/wallpapers/wallpaper_layout.h"

namespace wabar::wallpapers {

// The one Qt Quick item of the overlay scene: draws, for every monitor area, the old image and the
// new image revealed by the animation's shape at progress `t` (0..1), as scene graph nodes. The
// images become textures once; a frame only changes the triangles of the new image (the reveal
// polygon of `computeAreaFrame`, with texture coordinates) and, for a fade, its opacity. Nothing is
// rasterized per frame and no CPU frame buffer is kept. `paintWallpaperFrame` stays the reference
// the tests compare with.
//
// Threads: the GUI thread owns the item and calls setContent/setProgress. updatePaintNode runs on
// the render thread while the GUI thread is blocked, reads the members and builds the nodes; the
// textures and nodes belong to the render thread and are freed with the node there.
class WallpaperScene : public QQuickItem {
  Q_OBJECT
 public:
  explicit WallpaperScene(QQuickItem* parent = nullptr);
  void setContent(std::vector<ScreenArea> areas, std::vector<ScaledImage> oldImages,
                  std::vector<ScaledImage> newImages, double dpr, QColor background,
                  QString animation);
  void setProgress(double value);
  double progress() const { return progress_; }

  // What the last scene graph syncs built (tests; written on the render thread).
  struct SyncStats {
    int syncs = 0;      // updatePaintNode calls
    int rebuilds = 0;   // of them, full rebuilds of the node tree
    int textures = 0;   // textures in the tree
    int triangles = 0;  // triangles of the new images in the last sync
    int areas = 0;
  };
  SyncStats syncStats() const;

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

 private:
  struct AreaContent {
    QRectF monitor;
    QRectF oldRect;  // where the old image is drawn, logical pixels
    QRectF newRect;
    QImage oldImage;  // shares the pixels of the transition's pixmap
    QImage newImage;
  };

  std::vector<AreaContent> areas_;
  QColor background_ = Qt::black;
  QString animation_;
  double progress_ = 0.0;
  int contentSerial_ = 0;  // bumped by setContent: the node tree is rebuilt

  std::atomic<int> syncs_{0};
  std::atomic<int> rebuilds_{0};
  std::atomic<int> textures_{0};
  std::atomic<int> triangles_{0};
  std::atomic<int> areaCount_{0};
};

}  // namespace wabar::wallpapers

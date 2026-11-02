// The scene graph overlay item: the textures are made once per distinct image, a new progress
// changes the triangles of the new image and nothing else, new content rebuilds the tree. The
// tests run on the software scene graph, which builds the node tree (updatePaintNode runs) but
// draws no triangles; the look is compared through the geometry in test_wallpaper_reveal.cpp.
#include <gtest/gtest.h>

#include <QImage>
#include <QPixmap>
#include <QQuickWindow>
#include <memory>
#include <vector>

#include "features/wallpapers/qml/wallpaper_scene.h"

namespace {

using waku::wallpapers::ScaledImage;
using waku::wallpapers::ScreenArea;
using waku::wallpapers::WallpaperScene;

QPixmap solid(QRgb color, QSize size) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(color);
  return QPixmap::fromImage(image);
}

// Two 100x100 monitors side by side; every monitor has its own old and new picture, or all share
// one of each (a span layout).
struct Fixture {
  explicit Fixture(bool shared, const char* animation) {
    window = std::make_unique<QQuickWindow>();
    window->resize(200, 100);
    scene = new WallpaperScene(window->contentItem());
    scene->setSize(QSizeF(200, 100));
    std::vector<ScreenArea> areas{ScreenArea{0, 0, 100, 100, 1.0},
                                  ScreenArea{100, 0, 100, 100, 1.0}};
    const QPixmap oldA = solid(0xFFFF0000, QSize(100, 100));
    const QPixmap newA = solid(0xFF0000FF, QSize(100, 100));
    std::vector<ScaledImage> olds{{oldA, 0, 0},
                                  {shared ? oldA : solid(0xFFFF8000, QSize(100, 100)), 0, 0}};
    std::vector<ScaledImage> news{{newA, 0, 0},
                                  {shared ? newA : solid(0xFF00FF00, QSize(100, 100)), 0, 0}};
    scene->setContent(areas, olds, news, 1.0, Qt::black, QString::fromLatin1(animation));
  }

  void sync() { window->grabWindow(); }

  std::unique_ptr<QQuickWindow> window;
  WallpaperScene* scene = nullptr;
};

}  // namespace

TEST(WallpaperScene, BuildsOneTexturePerDistinctImage) {
  Fixture own(false, "circle");
  own.scene->setProgress(0.5);
  own.sync();
  auto stats = own.scene->syncStats();
  EXPECT_GE(stats.syncs, 1);
  EXPECT_EQ(stats.rebuilds, 1);
  EXPECT_EQ(stats.areas, 2);
  EXPECT_EQ(stats.textures, 4);  // old and new of each monitor

  Fixture span(true, "circle");
  span.scene->setProgress(0.5);
  span.sync();
  stats = span.scene->syncStats();
  EXPECT_EQ(stats.areas, 2);
  EXPECT_EQ(stats.textures, 2);  // both monitors draw the same two pictures
}

TEST(WallpaperScene, AFrameChangesTheTrianglesAndKeepsTheTextures) {
  Fixture f(false, "diamond");
  f.scene->setProgress(0.25);
  f.sync();
  const auto first = f.scene->syncStats();
  ASSERT_EQ(first.rebuilds, 1);
  EXPECT_EQ(first.triangles, 2 * 2);  // a diamond is a quad, twice
  f.scene->setProgress(1.0);
  f.sync();
  const auto last = f.scene->syncStats();
  EXPECT_GT(last.syncs, first.syncs);
  EXPECT_EQ(last.rebuilds, 1);  // same tree, new geometry
  EXPECT_EQ(last.textures, first.textures);
  EXPECT_EQ(last.triangles, 2 * 2);  // the whole monitor is a quad per monitor
  EXPECT_EQ(f.scene->progress(), 1.0);

  Fixture circle(false, "circle");
  circle.scene->setProgress(0.5);
  circle.sync();
  EXPECT_GT(circle.scene->syncStats().triangles, 2 * 14);  // a fan with many segments
}

TEST(WallpaperScene, AFadeIsOneQuadPerMonitor) {
  Fixture f(false, "fade");
  f.scene->setProgress(0.3);
  f.sync();
  EXPECT_EQ(f.scene->syncStats().triangles, 2 * 2);
  EXPECT_EQ(f.scene->syncStats().textures, 4);
}

TEST(WallpaperScene, NewContentRebuildsTheTree) {
  Fixture f(false, "split");
  f.scene->setProgress(0.5);
  f.sync();
  ASSERT_EQ(f.scene->syncStats().rebuilds, 1);
  f.scene->setContent(
      {ScreenArea{0, 0, 100, 100, 1.0}}, {{solid(0xFFFF0000, QSize(100, 100)), 0, 0}},
      {{solid(0xFF0000FF, QSize(100, 100)), 0, 0}}, 1.0, Qt::black, QStringLiteral("circle"));
  f.sync();
  const auto stats = f.scene->syncStats();
  EXPECT_EQ(stats.rebuilds, 2);
  EXPECT_EQ(stats.areas, 1);
  EXPECT_EQ(stats.textures, 2);
}

TEST(WallpaperScene, NoContentBuildsNoNodes) {
  QQuickWindow window;
  window.resize(10, 10);
  auto* scene = new WallpaperScene(window.contentItem());
  scene->setSize(QSizeF(10, 10));
  scene->setProgress(0.5);
  window.grabWindow();
  EXPECT_EQ(scene->syncStats().areas, 0);
  EXPECT_EQ(scene->syncStats().textures, 0);
}

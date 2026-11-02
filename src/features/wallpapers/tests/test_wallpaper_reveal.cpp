// The reveal geometry of the scene graph overlay against the reference: the covered area and the
// covered pixels of every reveal shape must match `revealPath` (what `paintWallpaperFrame` clips
// with), and the new image's triangles must stay inside the image.
#include <gtest/gtest.h>

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <cmath>
#include <vector>

#include "features/wallpapers/wallpaper_layout.h"
#include "features/wallpapers/wallpaper_reveal.h"

namespace {

using waku::wallpapers::AreaFrame;
using waku::wallpapers::clipConvex;
using waku::wallpapers::computeAreaFrame;
using waku::wallpapers::polygonArea;
using waku::wallpapers::revealPath;
using waku::wallpapers::revealPieces;

struct Monitor {
  int x, y, w, h;
  QRectF rect() const { return QRectF(x, y, w, h); }
};

// Two monitors of a desktop: a 100x100 square at the origin and a wide one next to it.
const Monitor kMonitors[] = {{0, 0, 100, 100}, {100, 0, 240, 135}, {37, 11, 301, 77}};
const double kProgress[] = {0.0, 0.1, 0.25, 0.5, 0.75, 0.99, 1.0};
const char* const kShapes[] = {"circle", "slide_top", "diamond", "split"};

double pathArea(const QPainterPath& path) {
  double area = 0;
  for (const QPolygonF& polygon : path.toSubpathPolygons()) area += polygonArea(polygon);
  return area;
}

double piecesArea(const std::vector<QPolygonF>& pieces) {
  double area = 0;
  for (const QPolygonF& piece : pieces) area += polygonArea(piece);
  return area;
}

QPainterPath monitorPath(const Monitor& m) {
  QPainterPath path;
  path.addRect(m.rect());
  return path;
}

// Pixels the pieces cover, no antialiasing, in the size of the monitor.
QImage rasterPieces(const std::vector<QPolygonF>& pieces, const Monitor& m) {
  QImage image(m.w, m.h, QImage::Format_Grayscale8);
  image.fill(0);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.translate(-m.x, -m.y);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::white);
  for (const QPolygonF& piece : pieces) painter.drawPolygon(piece);
  return image;
}

QImage rasterPath(const QPainterPath& path, const Monitor& m) {
  QImage image(m.w, m.h, QImage::Format_Grayscale8);
  image.fill(0);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.translate(-m.x, -m.y);
  painter.setClipRect(m.rect());
  painter.setClipPath(path, Qt::IntersectClip);
  painter.fillRect(m.rect(), Qt::white);
  return image;
}

}  // namespace

TEST(WallpaperReveal, TheCoveredAreaMatchesTheReferencePath) {
  for (const char* shape : kShapes) {
    for (const Monitor& m : kMonitors) {
      for (double t : kProgress) {
        const QString name = QString::fromLatin1(shape);
        const double reference =
            pathArea(revealPath(name, m.x, m.y, m.w, m.h, t).intersected(monitorPath(m)));
        const double got = piecesArea(revealPieces(name, m.x, m.y, m.w, m.h, t));
        // The circle is a polygon inside the true circle; everything else is exact.
        EXPECT_NEAR(got, reference, 0.01 * reference + 1.0)
            << shape << " t=" << t << " monitor " << m.w << "x" << m.h;
        EXPECT_LE(got, m.w * m.h + 1e-6);
      }
    }
  }
}

TEST(WallpaperReveal, TheCoveredPixelsMatchTheReferenceClip) {
  for (const char* shape : kShapes) {
    for (const Monitor& m : kMonitors) {
      for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const QString name = QString::fromLatin1(shape);
        const QImage want = rasterPath(revealPath(name, m.x, m.y, m.w, m.h, t), m);
        const QImage got = rasterPieces(revealPieces(name, m.x, m.y, m.w, m.h, t), m);
        int different = 0;
        for (int y = 0; y < m.h; ++y)
          for (int x = 0; x < m.w; ++x)
            if ((qGray(want.pixel(x, y)) > 127) != (qGray(got.pixel(x, y)) > 127)) ++different;
        // Only the one-pixel rim of a curve may differ.
        EXPECT_LE(different, 0.02 * m.w * m.h + 4) << shape << " t=" << t;
      }
    }
  }
}

TEST(WallpaperReveal, ProgressZeroRevealsAtMostASliverAndOneEverything) {
  for (const char* shape : kShapes) {
    const Monitor m{0, 0, 200, 100};
    const QString name = QString::fromLatin1(shape);
    EXPECT_LT(piecesArea(revealPieces(name, m.x, m.y, m.w, m.h, 0.0)), 0.03 * m.w * m.h) << shape;
    EXPECT_NEAR(piecesArea(revealPieces(name, m.x, m.y, m.w, m.h, 1.0)), m.w * m.h, 1e-6) << shape;
    EXPECT_NEAR(piecesArea(revealPieces(name, m.x, m.y, m.w, m.h, 0.5)),
                pathArea(revealPath(name, m.x, m.y, m.w, m.h, 0.5).intersected(monitorPath(m))),
                0.01 * m.w * m.h)
        << shape;
  }
}

TEST(WallpaperReveal, ThePiecesStayInsideTheMonitorAndAreConvex) {
  for (const char* shape : kShapes) {
    for (const Monitor& m : kMonitors) {
      for (double t : kProgress) {
        for (const QPolygonF& piece :
             revealPieces(QString::fromLatin1(shape), m.x, m.y, m.w, m.h, t)) {
          ASSERT_GE(piece.size(), 3);
          EXPECT_TRUE(m.rect().adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(piece.boundingRect()))
              << shape << " t=" << t;
          // Same turning direction at every corner.
          int sign = 0;
          for (int i = 0; i < piece.size(); ++i) {
            const QPointF a = piece[i];
            const QPointF b = piece[(i + 1) % piece.size()];
            const QPointF c = piece[(i + 2) % piece.size()];
            const double cross =
                (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
            if (std::abs(cross) < 1e-9) continue;
            const int s = cross > 0 ? 1 : -1;
            if (sign == 0) sign = s;
            EXPECT_EQ(s, sign) << shape << " t=" << t;
          }
        }
      }
    }
  }
}

TEST(WallpaperReveal, FadesHaveNoShapeAndBlendByProgress) {
  const QRectF monitor(0, 0, 100, 100);
  for (const char* name : {"fade", "wobble", ""}) {
    EXPECT_TRUE(revealPieces(QString::fromLatin1(name), 0, 0, 100, 100, 0.5).empty()) << name;
    for (double t : {0.0, 0.3, 1.0}) {
      const AreaFrame frame =
          computeAreaFrame(monitor, monitor, monitor, QString::fromLatin1(name), t);
      EXPECT_DOUBLE_EQ(frame.freshOpacity, t) << name;
      EXPECT_TRUE(frame.fill.empty()) << name;
      ASSERT_EQ(frame.fresh.size(), 1u) << name;
      EXPECT_NEAR(polygonArea(frame.fresh.front()), 10000.0, 1e-6) << name;
      EXPECT_EQ(frame.showOld, t < 1.0) << name;
    }
  }
  // Progress outside 0..1 cannot over- or undershoot the opacity.
  EXPECT_DOUBLE_EQ(
      computeAreaFrame(monitor, monitor, monitor, QStringLiteral("fade"), 1.2).freshOpacity, 1.0);
  EXPECT_DOUBLE_EQ(
      computeAreaFrame(monitor, monitor, monitor, QStringLiteral("fade"), -0.2).freshOpacity, 0.0);
}

TEST(WallpaperReveal, TheOldImageShowsUntilTheRevealIsComplete) {
  const QRectF monitor(0, 0, 100, 100);
  for (const char* shape : kShapes) {
    EXPECT_TRUE(
        computeAreaFrame(monitor, monitor, monitor, QString::fromLatin1(shape), 0.99).showOld);
    EXPECT_FALSE(
        computeAreaFrame(monitor, monitor, monitor, QString::fromLatin1(shape), 1.0).showOld);
  }
}

TEST(WallpaperReveal, TheNewImageIsClippedToItsOwnRectangleAndTheFillIsNot) {
  // A "fit" layout: the new image is a 60x40 band in the middle of a 100x100 monitor.
  const QRectF monitor(0, 0, 100, 100);
  const QRectF image(20, 30, 60, 40);
  const AreaFrame frame = computeAreaFrame(monitor, image, image, QStringLiteral("circle"), 1.0);
  EXPECT_NEAR(piecesArea(frame.fill), 10000.0, 1e-6);
  EXPECT_NEAR(piecesArea(frame.fresh), 2400.0, 1e-6);
  for (const QPolygonF& piece : frame.fresh)
    EXPECT_TRUE(image.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(piece.boundingRect()));
  EXPECT_EQ(frame.oldRect, image);

  // An image larger than the monitor (fill): the visible part is the monitor.
  const QRectF big(-50, -20, 300, 150);
  const AreaFrame filled = computeAreaFrame(monitor, big, big, QStringLiteral("split"), 0.5);
  EXPECT_EQ(filled.oldRect, monitor);
  EXPECT_NEAR(piecesArea(filled.fresh), piecesArea(filled.fill), 1e-6);

  // An image that misses the monitor draws nothing of its own.
  const QRectF away(500, 500, 40, 40);
  const AreaFrame missed = computeAreaFrame(monitor, away, away, QStringLiteral("circle"), 0.5);
  EXPECT_TRUE(missed.fresh.empty());
  EXPECT_TRUE(missed.oldRect.isEmpty());
  EXPECT_FALSE(missed.fill.empty());
}

TEST(WallpaperReveal, ClipConvexKeepsTheOverlapOnly) {
  const QPolygonF square({QPointF(0, 0), QPointF(10, 0), QPointF(10, 10), QPointF(0, 10)});
  EXPECT_NEAR(polygonArea(clipConvex(square, QRectF(5, 5, 20, 20))), 25.0, 1e-9);
  EXPECT_NEAR(polygonArea(clipConvex(square, QRectF(-5, -5, 30, 30))), 100.0, 1e-9);
  EXPECT_TRUE(clipConvex(square, QRectF(20, 20, 5, 5)).isEmpty());
  EXPECT_TRUE(clipConvex(QPolygonF(), QRectF(0, 0, 5, 5)).isEmpty());
}

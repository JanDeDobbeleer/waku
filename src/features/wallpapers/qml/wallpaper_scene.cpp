#include "features/wallpapers/qml/wallpaper_scene.h"

#include <QDebug>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGTexture>
#include <QSGTextureMaterial>
#include <exception>
#include <memory>
#include <utility>

#include "features/wallpapers/wallpaper_reveal.h"

namespace wabar::wallpapers {
namespace {

QPolygonF rectPolygon(const QRectF& r) {
  return QPolygonF({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
}

// A textured triangle list: every polygon is convex and becomes a fan.
class ImageNode : public QSGGeometryNode {
 public:
  ImageNode(QSGTexture* texture, bool opaque)
      : geometry_(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0) {
    geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
    setGeometry(&geometry_);
    QSGOpaqueTextureMaterial* material =
        opaque ? new QSGOpaqueTextureMaterial : new QSGTextureMaterial;
    material->setTexture(texture);
    material->setFiltering(QSGTexture::Linear);
    setMaterial(material);
    setFlag(OwnsMaterial);
  }

  // The polygons as triangles; texture coordinates run over `image`, the rectangle the whole
  // texture covers.
  void setPolygons(const std::vector<QPolygonF>& polygons, const QRectF& image) {
    int triangles = 0;
    for (const QPolygonF& p : polygons) triangles += fanTriangleCount(p);
    geometry_.allocate(triangles * 3);
    QSGGeometry::TexturedPoint2D* v = geometry_.vertexDataAsTexturedPoint2D();
    auto put = [&](const QPointF& p) {
      v->set(static_cast<float>(p.x()), static_cast<float>(p.y()),
             static_cast<float>((p.x() - image.x()) / image.width()),
             static_cast<float>((p.y() - image.y()) / image.height()));
      ++v;
    };
    for (const QPolygonF& p : polygons) {
      for (int i = 1; i + 1 < p.size(); ++i) {
        put(p[0]);
        put(p[i]);
        put(p[i + 1]);
      }
    }
    geometry_.markVertexDataDirty();
    markDirty(DirtyGeometry);
  }

 private:
  QSGGeometry geometry_;
};

// The background colour under the new image, as a triangle list.
class FillNode : public QSGGeometryNode {
 public:
  explicit FillNode(const QColor& color) : geometry_(QSGGeometry::defaultAttributes_Point2D(), 0) {
    geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
    setGeometry(&geometry_);
    auto* material = new QSGFlatColorMaterial;
    material->setColor(color);
    setMaterial(material);
    setFlag(OwnsMaterial);
  }

  void setPolygons(const std::vector<QPolygonF>& polygons) {
    int triangles = 0;
    for (const QPolygonF& p : polygons) triangles += fanTriangleCount(p);
    geometry_.allocate(triangles * 3);
    QSGGeometry::Point2D* v = geometry_.vertexDataAsPoint2D();
    for (const QPolygonF& p : polygons) {
      for (int i = 1; i + 1 < p.size(); ++i) {
        (v++)->set(static_cast<float>(p[0].x()), static_cast<float>(p[0].y()));
        (v++)->set(static_cast<float>(p[i].x()), static_cast<float>(p[i].y()));
        (v++)->set(static_cast<float>(p[i + 1].x()), static_cast<float>(p[i + 1].y()));
      }
    }
    geometry_.markVertexDataDirty();
    markDirty(DirtyGeometry);
  }

 private:
  QSGGeometry geometry_;
};

// The node tree of one overlay. Render thread. The nodes are deleted before the textures they
// point at.
class SceneNode : public QSGNode {
 public:
  struct Part {
    ImageNode* old = nullptr;
    QSGOpacityNode* group = nullptr;
    FillNode* fill = nullptr;
    ImageNode* fresh = nullptr;
  };

  ~SceneNode() override {
    while (QSGNode* child = firstChild()) {
      removeChildNode(child);
      delete child;
    }
  }

  int serial = 0;
  std::vector<std::unique_ptr<QSGTexture>> textures;
  std::vector<Part> parts;
};

}  // namespace

WallpaperScene::WallpaperScene(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
}

void WallpaperScene::setContent(std::vector<ScreenArea> areas, std::vector<ScaledImage> oldImages,
                                std::vector<ScaledImage> newImages, double dpr, QColor background,
                                QString animation) {
  areas_.clear();
  for (size_t i = 0; i < areas.size(); ++i) {
    const ScreenArea& a = areas[i];
    AreaContent content;
    content.monitor = QRectF(a.x, a.y, a.w, a.h);
    auto place = [&](const std::vector<ScaledImage>& images, QRectF* rect, QImage* image) {
      if (i >= images.size() || images[i].pixmap.isNull()) return;
      const ScaledImage& scaled = images[i];
      // Where drawPixmap puts it: the offset is in device pixels, the size in logical ones.
      *rect = QRectF(QPointF(a.x + scaled.offsetX / dpr, a.y + scaled.offsetY / dpr),
                     scaled.pixmap.deviceIndependentSize());
      *image = scaled.pixmap.toImage();
    };
    place(oldImages, &content.oldRect, &content.oldImage);
    place(newImages, &content.newRect, &content.newImage);
    areas_.push_back(std::move(content));
  }
  background_ = background;
  animation_ = std::move(animation);
  ++contentSerial_;
  update();
}

void WallpaperScene::setProgress(double value) {
  progress_ = value;
  update();
}

WallpaperScene::SyncStats WallpaperScene::syncStats() const {
  SyncStats stats;
  stats.syncs = syncs_.load();
  stats.rebuilds = rebuilds_.load();
  stats.textures = textures_.load();
  stats.triangles = triangles_.load();
  stats.areas = areaCount_.load();
  return stats;
}

QSGNode* WallpaperScene::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  ++syncs_;
  std::unique_ptr<SceneNode> root(static_cast<SceneNode*>(oldNode));
  try {
    if (root && root->serial != contentSerial_) root.reset();
    QQuickWindow* win = window();
    if (!root) {
      if (areas_.empty() || !win) return nullptr;
      root = std::make_unique<SceneNode>();
      root->serial = contentSerial_;
      // One texture per distinct image: a span layout hands every monitor the same pixels.
      std::vector<std::pair<qint64, QSGTexture*>> known;
      auto textureFor = [&](const QImage& image) -> QSGTexture* {
        if (image.isNull()) return nullptr;
        for (const auto& entry : known)
          if (entry.first == image.cacheKey()) return entry.second;
        QSGTexture* texture = win->createTextureFromImage(image, QQuickWindow::TextureIsOpaque);
        if (!texture) {
          qWarning() << "Wallpaper overlay: the image is too large for a texture" << image.size();
          return nullptr;
        }
        root->textures.emplace_back(texture);
        known.emplace_back(image.cacheKey(), texture);
        return texture;
      };
      for (const AreaContent& area : areas_) {
        SceneNode::Part part;
        auto* areaNode = new QSGNode;
        root->appendChildNode(areaNode);
        if (QSGTexture* texture = textureFor(area.oldImage)) {
          part.old = new ImageNode(texture, true);
          areaNode->appendChildNode(part.old);
        }
        part.group = new QSGOpacityNode;
        areaNode->appendChildNode(part.group);
        part.fill = new FillNode(background_);
        part.group->appendChildNode(part.fill);
        if (QSGTexture* texture = textureFor(area.newImage)) {
          // The fade blends the image; the shapes cover the fill and need no blending.
          part.fresh = new ImageNode(texture, !isFadeAnimation(animation_));
          part.group->appendChildNode(part.fresh);
        }
        root->parts.push_back(part);
      }
      ++rebuilds_;
    }

    int triangles = 0;
    for (size_t i = 0; i < areas_.size() && i < root->parts.size(); ++i) {
      const AreaContent& area = areas_[i];
      const SceneNode::Part& part = root->parts[i];
      const AreaFrame frame =
          computeAreaFrame(area.monitor, area.oldRect, area.newRect, animation_, progress_);
      if (part.old) {
        std::vector<QPolygonF> quad;
        if (frame.showOld && !frame.oldRect.isEmpty()) quad.push_back(rectPolygon(frame.oldRect));
        part.old->setPolygons(quad, area.oldRect);
      }
      part.group->setOpacity(frame.freshOpacity);
      part.fill->setPolygons(frame.fill);
      if (part.fresh) {
        part.fresh->setPolygons(frame.fresh, area.newRect);
        for (const QPolygonF& p : frame.fresh) triangles += fanTriangleCount(p);
      }
    }
    textures_ = static_cast<int>(root->textures.size());
    triangles_ = triangles;
    areaCount_ = static_cast<int>(root->parts.size());
    return root.release();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Wallpaper overlay sync failed:" << error.what();
  } catch (...) {
    qWarning() << "Wallpaper overlay sync failed";
  }
  return nullptr;  // root (the old tree or the partial new one) is deleted here
}

}  // namespace wabar::wallpapers

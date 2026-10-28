#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QThreadPool>
#include <QVariantAnimation>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "widgets/wallpapers/gallery_layout.h"
#include "widgets/wallpapers/thumbnail_cache.h"
#include "widgets/wallpapers/wallpaper_manager.h"

namespace yasb::widgets {

class WallpaperGallery;

// A screen as the scanner needs it: Qt name and rectangle in physical desktop pixels.
struct GalleryScreen {
  QString name;
  QRect nativeRect;
};

struct ScanResult {
  QStringList files;
  QHash<QString, int> current;  // screen name -> index of its wallpaper in `files`
};

// Walks the wallpaper folders and finds each screen's current wallpaper in them. Runs on a pool
// thread, so it must not touch widgets.
using GalleryScanner =
    std::function<ScanResult(const QStringList& paths, const std::vector<GalleryScreen>& screens)>;

// The real scan: collectImageFiles plus IDesktopWallpaper::GetWallpaper through `backend`.
GalleryScanner makeSystemScanner(WallpaperManager::BackendFactory backend);

// The row of wallpapers inside a gallery window; paints the cards, handles the mouse.
class CardsView : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kCardAnimationMs = 320;
  static constexpr int kClickAlpha = 1;
  static constexpr int kNoticeFontSize = 13;
  static constexpr int kNoticePadding = 22;

  explicit CardsView(WallpaperGallery* gallery);

  double offset() const { return offset_; }
  int index() const { return index_; }
  std::optional<int> hovered() const { return hovered_; }
  int count() const;
  double relativeDistance(int index) const;
  std::vector<VisibleCard> visibleCards() const;
  QPolygonF project(const Placement& spot, double inset = 0.0) const;
  std::optional<int> cardAt(const QPointF& position) const;
  const std::vector<std::pair<int, QPolygonF>>& hitAreas() const { return hitAreas_; }

  void goTo(int index, bool animate = true);
  void moveBy(int delta);
  void stopAnimation();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void drawNotice(const QString& message, QPainter& painter);
  void paintCards(QPainter& painter);

  WallpaperGallery* gallery_;
  double offset_ = 0.0;
  int index_ = 0;
  std::optional<int> hovered_;
  std::vector<std::pair<int, QPolygonF>> hitAreas_;
  QVariantAnimation* animation_;
};

// Port of services/wallpapers/gallery.py (GalleryWindow + Cards and its four types): a frameless,
// translucent window across the screen showing the wallpaper folder as a row of cards. Arrow
// keys, Page Up/Down, Home/End, the wheel and a double click navigate and choose; Escape or
// losing focus closes it; the context menu applies an image to one screen.
//
// Thread: GUI thread. A private QThreadPool (two threads) scans the folders and decodes
// thumbnails; its jobs only post queued calls back to the gallery, and ~WallpaperGallery clears
// the queue and waits for running jobs before the gallery is destroyed.
class WallpaperGallery : public QMainWindow {
  Q_OBJECT
 public:
  static constexpr int kFadeInMs = 80;
  static constexpr int kFadeOutMs = 120;

  struct Options {
    QStringList imagePaths;
    QString type = QStringLiteral("default");
    int imageWidth = 100;
    QString orientation = QStringLiteral("landscape");
    int cornerRadius = 0;
    QString accentColor = QStringLiteral("auto");
    QString styleSheet;
  };
  struct Services {
    WallpaperManager* manager = nullptr;
    GalleryScanner scanner;
    std::shared_ptr<ThumbnailCache> cache;
    // The Windows accent colour for accent_color "auto"; white when null.
    std::function<QColor()> systemAccent;
  };

  WallpaperGallery(Options options, Services services);
  ~WallpaperGallery() override;

  // Shows the gallery on `screen` (or the parent's, then the primary) with a short fade-in.
  void fadeIn(QWidget* parent, QScreen* screen);
  void fadeOutAndClose();
  bool isClosing() const { return closing_; }

  // The bare selection colour: the configured hex, or the Windows accent.
  const QColor& accent() const { return accent_; }
  const QStringList& imageFiles() const { return files_; }
  bool scanning() const { return scanning_; }
  const QStringList& imagePaths() const { return options_.imagePaths; }
  QSize cardSize() const { return cardSize_; }
  int cornerRadius() const { return options_.cornerRadius; }
  const CardStyle& style() const { return *style_; }
  int neighbours() const { return neighbours_; }
  bool wraps() const { return wraps_; }
  CardsView* view() const { return view_; }
  QString screenName() const { return screenName_; }
  std::optional<QString> selectedImage() const;
  bool hasPixmap(int index) const { return pixmaps_.contains(index); }

  std::vector<IndexedPlacement> layoutCards() const;
  QPixmap pixmapFor(int index);
  double normalise(double offset) const;
  double offsetFor(int index) const;
  void onFocusChanged(int index);
  int pageStep() const;

  void setWallpaper();
  void applyWallpaper(const QString& imagePath, const QString& monitorId);
  void showContextMenuForImage(int index, const QPoint& globalPos);

 protected:
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  friend class CardsView;
  QScreen* setupWindow(QWidget* parent, QScreen* screen);
  void buildContent(QScreen* screen);
  void fitToView(int width, int height);
  QSizeF thumbnailSize() const;
  void loadAround(int index);
  void requeue(const std::vector<int>& wanted, double width, double height);
  int priority(int target) const;
  void startLoader(int target, double width, double height);
  void onScanFinished(const QStringList& files, const QHash<QString, int>& current);
  void onImageLoaded(int index, const QImage& image);
  void onFadeOutFinished();

  Options options_;
  Services services_;
  std::unique_ptr<CardStyle> style_;
  QSize cardSize_;
  QColor accent_;
  QStringList files_;
  bool scanning_ = true;
  int neighbours_ = 1;
  bool wraps_ = false;
  double dpr_ = 1.0;
  QString screenName_;
  bool closing_ = false;
  bool menuOpen_ = false;

  // Thumbnails, least recently used first (index into files_).
  QHash<int, QPixmap> pixmaps_;
  QList<int> pixmapOrder_;
  QHash<int, bool> pending_;
  struct Queued {
    std::shared_ptr<std::atomic<int>> state;
  };
  QHash<int, Queued> queued_;
  QThreadPool pool_;
  CardsView* view_;
  QPointer<QPropertyAnimation> fadeAnimation_;
};

}  // namespace yasb::widgets

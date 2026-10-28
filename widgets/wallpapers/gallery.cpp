#include "widgets/wallpapers/gallery.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QImageIOHandler>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRunnable>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

#include "core/widgets/backdrop.h"
#include "platform/accent_color.h"
#include "platform/window_focus.h"
#include "widgets/wallpapers/wallpaper_files.h"

namespace yasb::widgets {
namespace {

int floorDiv(int a, int b) {
  int q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

QImage decodeThumbnail(const QString& path, double width, double height, double dpr,
                       const ThumbnailCache* cache) {
  const int targetW = static_cast<int>(std::ceil(width * dpr));
  const int targetH = static_cast<int>(std::ceil(height * dpr));

  QString key;
  if (cache) {
    key = cache->key(path, targetW, targetH);
    if (!key.isEmpty()) {
      if (auto cached = cache->load(key)) {
        cached->setDevicePixelRatio(dpr);
        return *cached;
      }
    }
  }

  QImageReader reader(path);
  reader.setAutoTransform(true);
  // size() and setScaledSize() are in the stored orientation, before the EXIF rotation.
  const bool sideways = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
  QSize original = reader.size();
  if (sideways) original.transpose();

  QSize scaledSize;
  if (!original.isValid()) {
    scaledSize = QSize(targetW, targetH);
  } else {
    const double origAspect = static_cast<double>(original.width()) / original.height();
    const double targetAspect = targetH != 0 ? static_cast<double>(targetW) / targetH : 1.0;
    if (origAspect > targetAspect) {
      scaledSize = QSize(static_cast<int>(targetH * origAspect), targetH);
    } else {
      scaledSize =
          QSize(targetW, origAspect != 0 ? static_cast<int>(targetW / origAspect) : targetH);
    }
  }
  reader.setScaledSize(sideways ? scaledSize.transposed() : scaledSize);
  const QImage image = reader.read();

  QImage thumbnail(targetW, targetH, QImage::Format_ARGB32_Premultiplied);
  thumbnail.fill(Qt::transparent);
  {
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const int x = floorDiv(targetW - image.width(), 2);
    const int y = floorDiv(targetH - image.height(), 2);
    const int sourceX = std::max(0, -x);
    const int sourceY = std::max(0, -y);
    const int sourceW = std::min(image.width() - sourceX, targetW);
    const int sourceH = std::min(image.height() - sourceY, targetH);
    painter.drawImage(QRect(std::max(0, x), std::max(0, y), sourceW, sourceH), image,
                      QRect(sourceX, sourceY, sourceW, sourceH));
  }
  thumbnail.setDevicePixelRatio(dpr);
  if (cache && !key.isEmpty() && !image.isNull()) cache->save(key, thumbnail);
  return thumbnail;
}

// Fills `rect` with `pixmap`, cropping rather than stretching it.
void drawCover(QPainter& painter, const QPixmap& pixmap, const QRectF& rect) {
  const QSizeF size = pixmap.deviceIndependentSize();
  const double scale = std::max(rect.width() / size.width(), rect.height() / size.height());
  const QPointF centre = rect.center();
  if (std::abs(scale - 1.0) < 0.01) {
    // Unscaled and on whole device pixels, QPainter copies the pixmap instead of resampling it.
    const QPaintDevice* device = painter.device();
    const double dpr = device ? device->devicePixelRatio() : 1.0;
    const double x = std::nearbyint((centre.x() - size.width() / 2.0) * dpr) / dpr;
    const double y = std::nearbyint((centre.y() - size.height() / 2.0) * dpr) / dpr;
    painter.drawPixmap(QPointF(x, y), pixmap);
    return;
  }
  const double width = size.width() * scale;
  const double height = size.height() * scale;
  const QRectF target(centre.x() - width / 2.0, centre.y() - height / 2.0, width, height);
  painter.drawPixmap(target, pixmap, QRectF(0, 0, pixmap.width(), pixmap.height()));
}

QRect nativeRectOf(const QScreen* screen) {
  const QRect geometry = screen->geometry();
  const double dpr = screen->devicePixelRatio();
  return QRect(geometry.topLeft(), QSize(static_cast<int>(std::lround(geometry.width() * dpr)),
                                         static_cast<int>(std::lround(geometry.height() * dpr))));
}

}  // namespace

GalleryScanner makeSystemScanner(WallpaperManager::BackendFactory backend) {
  return [backend = std::move(backend)](const QStringList& paths,
                                        const std::vector<GalleryScreen>& screens) {
    ScanResult result;
    try {
      result.files = collectImageFiles(paths);
    } catch (const std::exception& error) {
      qCritical().noquote() << "Failed to scan wallpaper folders:" << error.what();
    }
    if (result.files.isEmpty() || !backend) return result;
    try {
      const auto desktop = backend();
      const auto monitors = desktop->monitors();
      QHash<QString, QString> wallpapers;
      for (const GalleryScreen& screen : screens) {
        const platform::WallpaperMonitor* best = nullptr;
        qint64 bestArea = 0;
        for (const auto& monitor : monitors) {
          const QRect common = monitor.nativeRect.intersected(screen.nativeRect);
          const qint64 area = static_cast<qint64>(common.width()) * common.height();
          if (!common.isEmpty() && area > bestArea) {
            best = &monitor;
            bestArea = area;
          }
        }
        if (!best) {
          qDebug().noquote() << "Could not find the monitor of" << screen.name;
          continue;
        }
        wallpapers.insert(screen.name, desktop->wallpaperOf(best->id));
      }
      result.current = findWallpapers(result.files, wallpapers);
    } catch (const std::exception& error) {
      qDebug().noquote() << "Could not read the current wallpapers:" << error.what();
    }
    return result;
  };
}

// --- CardsView -------------------------------------------------------------------------------

CardsView::CardsView(WallpaperGallery* gallery) : QWidget(), gallery_(gallery) {
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_TranslucentBackground);
  setMouseTracking(true);

  animation_ = new QVariantAnimation(this);
  animation_->setDuration(kCardAnimationMs);
  animation_->setEasingCurve(QEasingCurve::OutCubic);
  connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    offset_ = value.toDouble();
    update();
  });
  connect(animation_, &QVariantAnimation::finished, this, [this] {
    // The selection was set when the move started; only the offset needs tidying.
    offset_ = gallery_->normalise(offset_);
    update();
  });
}

int CardsView::count() const { return static_cast<int>(gallery_->imageFiles().size()); }

double CardsView::relativeDistance(int index) const {
  return yasb::widgets::relativeDistance(index, offset_, count(), gallery_->wraps());
}

std::vector<VisibleCard> CardsView::visibleCards() const {
  return yasb::widgets::visibleCards(offset_, count(), gallery_->neighbours(), gallery_->wraps());
}

QPolygonF CardsView::project(const Placement& spot, double inset) const {
  return projectCard(spot, gallery_->cardSize(), size(), inset);
}

std::optional<int> CardsView::cardAt(const QPointF& position) const {
  for (auto it = hitAreas_.rbegin(); it != hitAreas_.rend(); ++it) {
    if (it->second.containsPoint(position, Qt::OddEvenFill)) return it->first;
  }
  return std::nullopt;
}

void CardsView::drawNotice(const QString& message, QPainter& painter) {
  // The window is translucent, so text sits directly on the desktop and plain white is unreadable
  // over a light wallpaper. The panel gives it a background it controls.
  QFont font = painter.font();
  font.setPointSize(kNoticeFontSize);
  painter.setFont(font);

  const int align = Qt::AlignCenter;
  const QRectF textArea = painter.boundingRect(rect(), align, message);
  const QRectF panel =
      textArea.adjusted(-kNoticePadding, -kNoticePadding, kNoticePadding, kNoticePadding);

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0, 0, 0, 190));
  painter.drawRoundedRect(panel, 12, 12);

  painter.setPen(QColor(255, 255, 255, 235));
  painter.drawText(rect(), align, message);
}

void CardsView::paintEvent(QPaintEvent*) {
  try {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintCards(painter);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Wallpaper gallery paint failed:" << error.what();
  }
}

void CardsView::paintCards(QPainter& painter) {
  const int border = gallery_->style().border();
  hitAreas_.clear();

  if (gallery_->imageFiles().isEmpty()) {
    if (!gallery_->scanning()) {
      drawNotice(QStringLiteral("No wallpapers found in\n") +
                     gallery_->imagePaths().join(QLatin1Char('\n')),
                 painter);
    }
    return;
  }

  if (kClickAlpha) painter.fillRect(rect(), QColor(0, 0, 0, kClickAlpha));

  for (const IndexedPlacement& card : gallery_->layoutCards()) {
    const int index = card.index;
    const Placement& spot = card.spot;
    const QPolygonF quad = project(spot);
    if (spot.opacity <= 0.01) continue;
    const QRectF bounds = quad.boundingRect();
    if (bounds.right() < 0 || bounds.left() > width()) continue;
    if (bounds.bottom() < 0 || bounds.top() > height()) continue;

    const QPolygonF picture = border ? project(spot, border) : quad;
    const QRectF area = picture.boundingRect();
    const double focus = spot.focus;

    painter.setOpacity(spot.opacity);
    painter.save();
    QPainterPath window;
    const int radius = gallery_->cornerRadius();
    if (spot.shear != 0.0 || radius <= 0) {
      // A leaning card is a parallelogram; a rounded rect cannot follow it.
      window.addPolygon(picture);
      window.closeSubpath();
    } else {
      // Same rect and radius as the border, so the two stay concentric.
      window.addRoundedRect(area, radius, radius);
    }
    painter.setClipPath(window);

    const QPixmap pixmap = gallery_->pixmapFor(index);
    if (!pixmap.isNull()) {
      drawCover(painter, pixmap, area);
    } else {
      painter.fillRect(area, QColor(255, 255, 255, 18));
    }

    const int shade = static_cast<int>(255 * gallery_->style().dim() * (1.0 - focus));
    if (shade) painter.fillRect(area, QColor(0, 0, 0, shade));
    painter.restore();

    // Cubed, so a card mid-move barely shows a border and only one looks selected.
    const double strength = (hovered_ && *hovered_ == index) ? 1.0 : std::pow(focus, 3);
    if (border && strength > 0.01) {
      const QPolygonF edge = project(spot, border / 2.0);
      QPainterPath outline;
      if (spot.shear != 0.0 || radius <= 0) {
        outline.addPolygon(edge);
        outline.closeSubpath();
      } else {
        const double corner = radius + border / 2.0;
        outline.addRoundedRect(edge.boundingRect(), corner, corner);
      }
      QColor accent = gallery_->accent();
      accent.setAlpha(static_cast<int>(255 * strength));
      QPen pen(accent);
      pen.setWidthF(border);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(outline);
    }

    hitAreas_.emplace_back(index, quad);
  }
}

void CardsView::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  gallery_->fitToView(width(), height());
}

void CardsView::goTo(int index, bool animate) {
  const int total = count();
  if (!total) return;

  index_ = ((index % total) + total) % total;
  const double target = gallery_->offsetFor(index_);
  animation_->stop();

  if (!animate) {
    offset_ = target;
    gallery_->onFocusChanged(index_);
    update();
    return;
  }

  gallery_->onFocusChanged(index_);
  animation_->setStartValue(offset_);
  animation_->setEndValue(target);
  animation_->start();
}

void CardsView::moveBy(int delta) {
  const int total = count();
  if (!total) return;
  if (gallery_->wraps())
    goTo(index_ + delta);
  else
    goTo(std::max(0, std::min(total - 1, index_ + delta)));
}

void CardsView::stopAnimation() { animation_->stop(); }

void CardsView::wheelEvent(QWheelEvent* event) {
  const int delta = event->angleDelta().y();
  if (delta) {
    moveBy(delta > 0 ? -1 : 1);
    event->accept();
  }
}

void CardsView::mouseMoveEvent(QMouseEvent* event) {
  const std::optional<int> hovered = cardAt(event->position());
  if (hovered != hovered_) {
    hovered_ = hovered;
    update();
  }
}

void CardsView::leaveEvent(QEvent*) {
  if (hovered_) {
    hovered_.reset();
    update();
  }
}

void CardsView::mousePressEvent(QMouseEvent* event) {
  // Clicking never moves the cards, so a double click acts on what you aimed at.
  event->accept();
}

void CardsView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  if (const auto index = cardAt(event->position())) {
    gallery_->applyWallpaper(gallery_->imageFiles().at(*index), QString());
  }
  event->accept();
}

void CardsView::contextMenuEvent(QContextMenuEvent* event) {
  const bool mouse = event->reason() == QContextMenuEvent::Mouse;
  const std::optional<int> index =
      mouse ? cardAt(QPointF(event->pos())) : std::optional<int>(index_);
  if (index) gallery_->showContextMenuForImage(*index, event->globalPos());
}

// --- WallpaperGallery ------------------------------------------------------------------------

WallpaperGallery::WallpaperGallery(Options options, Services services)
    : QMainWindow(nullptr), options_(std::move(options)), services_(std::move(services)) {
  style_ = makeCardStyle(options_.type);
  cardSize_ = cardSizeFor(options_.imageWidth, options_.orientation);
  if (options_.accentColor != QLatin1String("auto")) {
    accent_ = QColor(options_.accentColor);
  } else {
    const QColor system = services_.systemAccent ? services_.systemAccent() : QColor();
    accent_ = system.isValid() ? system : QColor(255, 255, 255);
  }
  pool_.setMaxThreadCount(2);
  view_ = new CardsView(this);

  // Started here rather than on show, so the walk has a head start on the window being built and
  // the fade-in running.
  std::vector<GalleryScreen> screens;
  for (const QScreen* screen : QGuiApplication::screens())
    screens.push_back({screen->name(), nativeRectOf(screen)});
  const QStringList paths = options_.imagePaths;
  GalleryScanner scanner = services_.scanner;
  std::shared_ptr<ThumbnailCache> cache = services_.cache;
  pool_.start(QRunnable::create([this, paths, screens, scanner, cache] {
    ScanResult result;
    try {
      if (scanner) result = scanner(paths, screens);
    } catch (const std::exception& error) {
      qCritical().noquote() << "Failed to scan wallpaper folders:" << error.what();
    }
    QMetaObject::invokeMethod(
        this, [this, result] { onScanFinished(result.files, result.current); },
        Qt::QueuedConnection);
    if (cache) cache->prune();
  }));
}

WallpaperGallery::~WallpaperGallery() {
  // Queued results for this window are dropped with it; running jobs finish first.
  pool_.clear();
  pool_.waitForDone();
}

std::optional<QString> WallpaperGallery::selectedImage() const {
  if (files_.isEmpty()) return std::nullopt;
  return files_.at(view_->index());
}

QScreen* WallpaperGallery::setupWindow(QWidget* parent, QScreen* screen) {
  if (parent && !screen) {
    if (const QWidget* window = parent->window()) screen = window->screen();
    if (!screen) screen = QApplication::screenAt(parent->mapToGlobal(QPoint(0, 0)));
    if (!screen) screen = parent->screen();
  }
  if (!screen) screen = QApplication::primaryScreen();
  if (!screen) throw std::runtime_error("No screen to show the wallpaper gallery on");

  dpr_ = screen->devicePixelRatio();
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::StrongFocus);
  return screen;
}

void WallpaperGallery::buildContent(QScreen* screen) {
  screenName_ = screen->name();
  const QRect area = screen->geometry();
  const int height =
      std::min(area.height(), static_cast<int>(cardSize_.height() * style_->maxScale()));
  setGeometry(area.x(), area.y() + (area.height() - height) / 2, area.width(), height);
  fitToView(area.width(), height);
  onFocusChanged(0);
}

void WallpaperGallery::fadeIn(QWidget* parent, QScreen* screen) {
  for (QWidget* widget : QApplication::topLevelWidgets()) {
    auto* other = qobject_cast<WallpaperGallery*>(widget);
    if (other && other != this && other->isVisible()) other->fadeOutAndClose();
  }

  screen = setupWindow(parent, screen);
  setCentralWidget(view_);
  setStyleSheet(options_.styleSheet);
  buildContent(screen);

  auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
  animation->setDuration(kFadeInMs);
  animation->setStartValue(0.0);
  animation->setEndValue(1.0);
  fadeAnimation_ = animation;
  setWindowOpacity(0.0);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
  show();
}

void WallpaperGallery::fadeOutAndClose() {
  if (closing_) return;
  closing_ = true;

  auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
  animation->setDuration(kFadeOutMs);
  animation->setStartValue(1.0);
  animation->setEndValue(0.0);
  connect(animation, &QPropertyAnimation::finished, this, &WallpaperGallery::onFadeOutFinished);
  fadeAnimation_ = animation;
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void WallpaperGallery::onFadeOutFinished() {
  view_->stopAnimation();
  pool_.clear();
  pixmaps_.clear();
  pixmapOrder_.clear();
  hide();
  deleteLater();
}

void WallpaperGallery::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  // Only a real Windows window can take the foreground; offscreen test windows must not try.
  if (QGuiApplication::platformName() == QLatin1String("windows"))
    platform::forceForegroundFocus(winId());
}

void WallpaperGallery::changeEvent(QEvent* event) {
  if (event->type() == QEvent::ActivationChange) {
    if (!isActiveWindow() && !closing_ && !menuOpen_) fadeOutAndClose();
  }
  QMainWindow::changeEvent(event);
}

void WallpaperGallery::setWallpaper() {
  if (const auto image = selectedImage()) applyWallpaper(*image, QString());
}

void WallpaperGallery::applyWallpaper(const QString& imagePath, const QString& monitorId) {
  fadeOutAndClose();
  if (!services_.manager) return;
  if (monitorId.isEmpty())
    services_.manager->setWallpaper(imagePath);
  else
    services_.manager->setWallpaper(imagePath, monitorId, false);
}

void WallpaperGallery::showContextMenuForImage(int index, const QPoint& globalPos) {
  if (index < 0 || index >= files_.size()) return;
  const QString imagePath = files_.at(index);

  QPointer<WallpaperGallery> self(this);
  QMenu menu(this);
  // apply_qmenu_style: Fusion removes the menu shadow, blur and round corners on show.
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    fusion->setParent(&menu);
    menu.setStyle(fusion);
  }
  connect(&menu, &QMenu::aboutToShow, &menu, [&menu] {
    try {
      enableBlur(menu.winId(), false, true, QStringLiteral("normal"), QStringLiteral("None"));
    } catch (...) {
    }
  });
  menu.setProperty("class", QStringLiteral("context-menu"));

  QAction* all = menu.addAction(QStringLiteral("Set on all screens"));
  connect(all, &QAction::triggered, this,
          [this, imagePath] { applyWallpaper(imagePath, QString()); });

  const QStringList monitorIds =
      services_.manager ? services_.manager->monitorIds() : QStringList();
  if (monitorIds.size() > 1) {
    menu.addSeparator();
    for (qsizetype position = 0; position < monitorIds.size(); ++position) {
      const QString monitorId = monitorIds.at(position);
      QAction* action = menu.addAction(QStringLiteral("Set on screen %1").arg(position + 1));
      connect(action, &QAction::triggered, this,
              [this, imagePath, monitorId] { applyWallpaper(imagePath, monitorId); });
    }
  }

  menuOpen_ = true;
  menu.exec(globalPos);
  if (!self) return;
  menuOpen_ = false;
  if (!closing_ && !isActiveWindow()) fadeOutAndClose();
}

std::vector<IndexedPlacement> WallpaperGallery::layoutCards() const {
  return style_->layoutCards(view_->visibleCards(), cardSize_.width());
}

double WallpaperGallery::normalise(double offset) const {
  return normaliseOffset(offset, static_cast<int>(files_.size()), wraps_);
}

double WallpaperGallery::offsetFor(int index) const {
  return view_->offset() + view_->relativeDistance(index);
}

void WallpaperGallery::fitToView(int width, int) {
  const FitResult fit = yasb::widgets::fitToView(width, style_->stepPixels(cardSize_.width()),
                                                 static_cast<int>(files_.size()));
  neighbours_ = fit.neighbours;
  wraps_ = fit.wraps;
}

int WallpaperGallery::pageStep() const {
  return pageStepFor(view_->width(), cardSize_.width(), style_->stepPixels(cardSize_.width()));
}

QSizeF WallpaperGallery::thumbnailSize() const {
  const QRectF area =
      view_->project(style_->selectedSpot(cardSize_.width()), style_->border()).boundingRect();
  return area.size();
}

QPixmap WallpaperGallery::pixmapFor(int index) {
  const auto it = pixmaps_.constFind(index);
  if (it == pixmaps_.constEnd()) return QPixmap();
  pixmapOrder_.removeOne(index);
  pixmapOrder_.append(index);
  return it.value();
}

void WallpaperGallery::onFocusChanged(int index) {
  if (!files_.isEmpty()) loadAround(index);
}

int WallpaperGallery::priority(int target) const {
  // Higher for the cards nearest the selection, so they decode first.
  const int total = static_cast<int>(files_.size());
  int distance = std::abs(target - view_->index());
  if (wraps_ && total) distance = std::min(distance, total - distance);
  return -distance;
}

void WallpaperGallery::startLoader(int target, double width, double height) {
  auto state = std::make_shared<std::atomic<int>>(0);
  const QString path = files_.at(target);
  const double dpr = dpr_;
  const std::shared_ptr<ThumbnailCache> cache = services_.cache;
  queued_.insert(target, Queued{state});
  pool_.start(QRunnable::create([this, state, path, width, height, dpr, cache, target] {
                // 0 queued, 1 started, 2 cancelled: a cancelled job that never started does
                // nothing.
                int expected = 0;
                if (!state->compare_exchange_strong(expected, 1)) return;
                QImage image;
                try {
                  image = decodeThumbnail(path, width, height, dpr, cache.get());
                } catch (const std::exception& error) {
                  qWarning().noquote() << "Wallpaper thumbnail failed:" << error.what();
                }
                QMetaObject::invokeMethod(
                    this, [this, target, image] { onImageLoaded(target, image); },
                    Qt::QueuedConnection);
              }),
              priority(target));
}

void WallpaperGallery::requeue(const std::vector<int>& wanted, double width, double height) {
  const QList<int> targets = queued_.keys();
  for (const int target : targets) {
    const auto it = queued_.find(target);
    if (it == queued_.end()) continue;
    if (pixmaps_.contains(target)) {
      queued_.erase(it);
      continue;
    }
    int expected = 0;
    if (!it->state->compare_exchange_strong(expected, 2)) continue;  // already running
    queued_.erase(it);
    if (std::find(wanted.begin(), wanted.end(), target) != wanted.end()) {
      startLoader(target, width, height);  // same job again, at its new priority
    } else {
      // Never started, so nothing will arrive to clear it.
      pending_.remove(target);
    }
  }
}

void WallpaperGallery::loadAround(int index) {
  const int total = static_cast<int>(files_.size());
  if (!total) return;

  const int radius = neighbours_ + 3;
  std::vector<int> wanted;
  for (int offset = -radius; offset <= radius; ++offset) {
    const int target = (((index + offset) % total) + total) % total;
    if (std::find(wanted.begin(), wanted.end(), target) == wanted.end()) wanted.push_back(target);
  }
  const QSizeF thumb = thumbnailSize();

  requeue(wanted, thumb.width(), thumb.height());

  std::vector<int> order = wanted;
  std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
    return std::abs(view_->relativeDistance(a)) < std::abs(view_->relativeDistance(b));
  });
  for (const int target : order) {
    if (pixmaps_.contains(target) || pending_.contains(target)) continue;
    pending_.insert(target, true);
    startLoader(target, thumb.width(), thumb.height());
  }

  const qsizetype limit = 2 * radius + 8;
  const QList<int> cached = pixmapOrder_;
  for (const int candidate : cached) {
    if (std::find(wanted.begin(), wanted.end(), candidate) != wanted.end()) continue;
    if (pixmaps_.size() <= limit) break;
    pixmaps_.remove(candidate);
    pixmapOrder_.removeOne(candidate);
  }
}

void WallpaperGallery::onImageLoaded(int index, const QImage& image) {
  pending_.remove(index);
  queued_.remove(index);
  if (closing_ || index >= files_.size()) return;
  pixmaps_.insert(index, QPixmap::fromImage(image));
  pixmapOrder_.removeOne(index);
  pixmapOrder_.append(index);
  if (std::abs(view_->relativeDistance(index)) <= neighbours_ + 0.5) view_->update();
}

void WallpaperGallery::onScanFinished(const QStringList& files,
                                      const QHash<QString, int>& current) {
  scanning_ = false;
  if (closing_) return;
  files_ = files;
  fitToView(view_->width(), view_->height());
  view_->goTo(current.value(screenName_, 0), false);
  view_->update();
}

void WallpaperGallery::keyPressEvent(QKeyEvent* event) {
  const int key = event->key();
  const int page = pageStep();

  if (key == Qt::Key_Escape) {
    fadeOutAndClose();
  } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    setWallpaper();
  } else if (key == Qt::Key_Left) {
    view_->moveBy(-1);
  } else if (key == Qt::Key_Right) {
    view_->moveBy(1);
  } else if (key == Qt::Key_PageUp) {
    view_->moveBy(-page);
  } else if (key == Qt::Key_PageDown) {
    view_->moveBy(page);
  } else if (key == Qt::Key_Home) {
    view_->goTo(0);
  } else if (key == Qt::Key_End) {
    view_->goTo(static_cast<int>(files_.size()) - 1);
  } else {
    QMainWindow::keyPressEvent(event);
  }
}

}  // namespace yasb::widgets

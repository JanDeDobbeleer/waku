#include "features/wallpapers/qml/wallpaper_transition.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <exception>
#include <utility>

namespace wabar::wallpapers {

void paintWallpaperFrame(QPainter& painter, const std::vector<ScreenArea>& areas,
                         const std::vector<ScaledImage>& oldImages,
                         const std::vector<ScaledImage>& newImages, double dpr,
                         const QColor& background, const QString& animation, double t) {
  const bool fade = animation != QLatin1String("circle") &&
                    animation != QLatin1String("slide_top") &&
                    animation != QLatin1String("diamond") && animation != QLatin1String("split");
  for (size_t i = 0; i < areas.size(); ++i) {
    const ScreenArea& a = areas[i];
    const QRectF monitor(a.x, a.y, a.w, a.h);

    if (t < 1.0) {
      painter.save();
      painter.setClipRect(monitor);
      painter.fillRect(monitor, background);
      if (i < oldImages.size()) {
        const ScaledImage& old = oldImages[i];
        painter.drawPixmap(QPointF(a.x + old.offsetX / dpr, a.y + old.offsetY / dpr), old.pixmap);
      }
      painter.restore();
    }

    if (i >= newImages.size()) continue;
    const ScaledImage& fresh = newImages[i];
    painter.save();
    painter.setClipRect(monitor);
    if (fade) {
      painter.setOpacity(std::clamp(t, 0.0, 1.0));
    } else {
      painter.setClipPath(revealPath(animation, a.x, a.y, a.w, a.h, t), Qt::IntersectClip);
    }
    if (!fade) painter.fillRect(monitor, background);
    painter.drawPixmap(QPointF(a.x + fresh.offsetX / dpr, a.y + fresh.offsetY / dpr), fresh.pixmap);
    painter.restore();
  }
}

// --- WallpaperScene

WallpaperScene::WallpaperScene(QQuickItem* parent) : QQuickPaintedItem(parent) {
  setAntialiasing(true);
  setOpaquePainting(true);
}

void WallpaperScene::setContent(std::vector<ScreenArea> areas, std::vector<ScaledImage> oldImages,
                                std::vector<ScaledImage> newImages, double dpr, QColor background,
                                QString animation) {
  areas_ = std::move(areas);
  old_ = std::move(oldImages);
  new_ = std::move(newImages);
  dpr_ = dpr;
  background_ = background;
  animation_ = std::move(animation);
  update();
}

void WallpaperScene::setProgress(double value) {
  progress_ = value;
  update();
}

void WallpaperScene::paint(QPainter* painter) {
  try {
    paintWallpaperFrame(*painter, areas_, old_, new_, dpr_, background_, animation_, progress_);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Wallpaper overlay paint failed:" << error.what();
  } catch (...) {
    qWarning() << "Wallpaper overlay paint failed";
  }
}

// --- WallpaperTransition

WallpaperTransition::Platform WallpaperTransition::systemPlatform() {
  Platform platform;
  platform.readSettings = [] { return readDesktopSettings(); };
  platform.oldImagePath = [] { return wabar::wallpapers::transcodedWallpaperPath(); };
  platform.attach = [](std::uintptr_t hwnd, QString* failure) {
    return wabar::wallpapers::attachToWorkerW(hwnd, failure);
  };
  platform.detach = [](std::uintptr_t hwnd) { wabar::wallpapers::detachFromWorkerW(hwnd); };
  platform.decode = [](const QString& newPath, const QString& oldPath) {
    QImageReader reader(newPath);
    reader.setAutoTransform(true);
    const QImage fresh = reader.read();
    const QImage old(oldPath);
    return std::make_pair(fresh, old);
  };
  return platform;
}

WallpaperTransition::WallpaperTransition(QString imagePath, QString animation, Platform platform,
                                         int animationMs, QObject* parent)
    : QObject(parent),
      imagePath_(std::move(imagePath)),
      animation_(std::move(animation)),
      platform_(std::move(platform)),
      animationMs_(animationMs) {
  if (platform_.readSettings) settings_ = platform_.readSettings();
}

WallpaperTransition::~WallpaperTransition() {
  destroyWindow();
  // Its result goes nowhere (the QPointer it posts through is null by then), but a decoder still
  // running at exit would outlive QCoreApplication.
  if (decoder_ && decoder_->isRunning()) decoder_->wait();
}

bool WallpaperTransition::windowVisible() const { return window_ && window_->isVisible(); }

void WallpaperTransition::start() {
  if (state_ != State::Idle) return;
  state_ = State::Decoding;
  const QString newPath = imagePath_;
  const QString oldPath = platform_.oldImagePath ? platform_.oldImagePath() : QString();
  auto decode = platform_.decode;
  QPointer<WallpaperTransition> self(this);
  QThread* thread = QThread::create([self, newPath, oldPath, decode] {
    std::pair<QImage, QImage> images;
    try {
      if (decode) images = decode(newPath, oldPath);
    } catch (...) {
    }
    // The QPointer is only dereferenced on the GUI thread, where it cannot dangle.
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self, images] {
          if (self) self->onImagesLoaded(images.first, images.second);
        },
        Qt::QueuedConnection);
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  decoder_ = thread;
  thread->start();
}

void WallpaperTransition::skip(const QString& reason) {
  destroyWindow();
  state_ = State::Skipped;
  emit skipped(reason);
  emit finished();
}

void WallpaperTransition::onImagesLoaded(const QImage& newImage, const QImage& oldImage) {
  if (state_ != State::Decoding && state_ != State::Idle) return;
  try {
    if (newImage.isNull() || oldImage.isNull()) {
      skip(QStringLiteral("Could not read the wallpaper images"));
      return;
    }
    const QPixmap pixmapNew = QPixmap::fromImage(newImage);
    const QPixmap pixmapOld = QPixmap::fromImage(oldImage);

    window_ = new QQuickWindow();
    window_->setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    window_->setColor(settings_.background);
    if (const QScreen* primary = QGuiApplication::primaryScreen())
      window_->setGeometry(primary->geometry());
    scene_ = new WallpaperScene(window_->contentItem());

    QString failure;
    const auto attachment =
        platform_.attach ? platform_.attach(window_->winId(), &failure) : std::nullopt;
    if (!attachment) {
      skip(failure.isEmpty() ? QStringLiteral("Could not attach to WorkerW") : failure);
      return;
    }
    attached_ = true;

    const double dpr = window_->devicePixelRatio() > 0 ? window_->devicePixelRatio() : 1.0;
    const QRect worker = attachment->workerRect;
    std::vector<ScreenArea> areas;
    for (const QRect& monitor : attachment->monitors) {
      ScreenArea area;
      area.x = roundHalfEven((monitor.left() - worker.left()) / dpr);
      area.y = roundHalfEven((monitor.top() - worker.top()) / dpr);
      area.w = roundHalfEven(monitor.width() / dpr);
      area.h = roundHalfEven(monitor.height() / dpr);
      area.dpr = dpr;
      areas.push_back(area);
    }
    if (!areas.empty()) {
      int minX = areas.front().x;
      int minY = areas.front().y;
      for (const auto& a : areas) {
        minX = std::min(minX, a.x);
        minY = std::min(minY, a.y);
      }
      int totalW = 0;
      int totalH = 0;
      for (auto& a : areas) {
        a.x -= minX;
        a.y -= minY;
        totalW = std::max(totalW, a.x + a.w);
        totalH = std::max(totalH, a.y + a.h);
      }
      size_ = QSize(totalW, totalH);
    } else {
      size_ = window_->size();
    }
    dpr_ = areas.empty() ? dpr : areas.front().dpr;
    areas_ = std::move(areas);
    scaledNew_ = layoutForScreens(pixmapNew, areas_, dpr_, settings_, size_);
    scaledOld_ = layoutForScreens(pixmapOld, areas_, dpr_, settings_, size_);

    window_->setPosition(0, 0);
    window_->resize(size_);
    scene_->setSize(size_);
    scene_->setContent(areas_, scaledOld_, scaledNew_, dpr_, settings_.background, animation_);
    scene_->setProgress(0.0);
    progress_ = 0.0;
    state_ = State::Playing;
    if (platform_.showWindow) {
      // A child of WorkerW is only composed when it is layered, and Qt layers a window once its
      // opacity is not 1; 0.999 cannot be told from 1.
      window_->setOpacity(0.999);
      window_->show();
    }

    animationTimer_ = new QVariantAnimation(this);
    animationTimer_->setDuration(std::max(1, animationMs_));
    animationTimer_->setStartValue(0.0);
    animationTimer_->setEndValue(1.0);
    animationTimer_->setEasingCurve(QEasingCurve::OutQuad);
    connect(animationTimer_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
      if (state_ == State::Playing) setProgress(value.toDouble());
    });
    connect(animationTimer_, &QVariantAnimation::finished, this, [this] {
      if (state_ != State::Playing) return;
      setProgress(1.0);
      state_ = State::Finished;
      lingerTimer_ = new QTimer(this);
      lingerTimer_->setSingleShot(true);
      connect(lingerTimer_, &QTimer::timeout, this, [this] { destroyWindow(); });
      lingerTimer_->start(kLingerMs);
      emit finished();
    });
    animationTimer_->start();
  } catch (const std::exception& error) {
    skip(QString::fromUtf8(error.what()));
  }
}

void WallpaperTransition::setProgress(double value) {
  progress_ = value;
  if (scene_) scene_->setProgress(value);
}

void WallpaperTransition::cancel() {
  if (state_ != State::Finished && state_ != State::Skipped) state_ = State::Cancelled;
  destroyWindow();
}

void WallpaperTransition::destroyWindow() {
  if (animationTimer_) animationTimer_->stop();
  if (lingerTimer_) lingerTimer_->stop();
  if (window_) {
    window_->hide();
    // Back to a top-level window first: the handle is a child of Explorer's WorkerW, and an
    // Explorer restart meanwhile may already have destroyed it (detach checks IsWindow).
    if (attached_ && platform_.detach) platform_.detach(window_->winId());
    attached_ = false;
    QQuickWindow* window = std::exchange(window_, nullptr);
    scene_ = nullptr;
    // Deferred: this can run from the animation's finished() emission or a QML handler of it.
    window->deleteLater();  // also deletes the scene item
  }
  // The laid-out images are two screen-size pixmaps per monitor (44 MB at 2880x1920): free them
  // with the window instead of holding them until the next change replaces this transition.
  std::vector<ScaledImage>().swap(scaledNew_);
  std::vector<ScaledImage>().swap(scaledOld_);
}

QImage WallpaperTransition::renderFrame() const {
  QImage image(size_.isValid() ? size_ : QSize(1, 1), QImage::Format_RGB32);
  image.fill(settings_.background);
  QPainter painter(&image);
  paintWallpaperFrame(painter, areas_, scaledOld_, scaledNew_, dpr_, settings_.background,
                      animation_, progress_);
  return image;
}

}  // namespace wabar::wallpapers

#include "widgets/wallpapers/wallpaper_engine.h"

#include <QApplication>
#include <QDebug>
#include <QEasingCurve>
#include <QImageReader>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <algorithm>

namespace yasb::widgets {

WallpaperEngine::Platform WallpaperEngine::systemPlatform() {
  Platform platform;
  platform.readSettings = [] { return readDesktopSettings(); };
  platform.transcodedPath = [] { return platform::transcodedWallpaperPath(); };
  platform.attach = [](std::uintptr_t hwnd, QString* failure) {
    return platform::attachToWorkerW(hwnd, failure);
  };
  return platform;
}

WallpaperEngine::WallpaperEngine(const QString& imagePath, const QString& animation,
                                 Platform platform, int animationMs)
    : QWidget(nullptr),
      imagePath_(imagePath),
      animation_(animation),
      platform_(std::move(platform)),
      animationMs_(animationMs) {
  if (platform_.readSettings) settings_ = platform_.readSettings();
  if (const QScreen* primary = QApplication::primaryScreen()) setGeometry(primary->geometry());
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setWindowFlag(Qt::FramelessWindowHint, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_DeleteOnClose, true);
  winId();

  timeline_ = new QTimeLine(animationMs_, this);
  timeline_->setUpdateInterval(kFrameMs);
  timeline_->setEasingCurve(QEasingCurve::OutQuad);
  connect(timeline_, &QTimeLine::valueChanged, this, &WallpaperEngine::setProgress);
  connect(timeline_, &QTimeLine::finished, this, &WallpaperEngine::onTimelineFinished);
}

WallpaperEngine::~WallpaperEngine() {
  if (timeline_) timeline_->stop();
  if (fadeTimeline_) fadeTimeline_->stop();
  if (loader_) {
    loader_->wait();
    delete loader_;
  }
}

void WallpaperEngine::start() {
  const QString newPath = imagePath_;
  const QString oldPath = platform_.transcodedPath ? platform_.transcodedPath() : QString();
  QPointer<WallpaperEngine> self(this);
  QThread* thread = QThread::create([self, newPath, oldPath] {
    QImageReader reader(newPath);
    reader.setAutoTransform(true);
    const QImage newImage = reader.read();
    const QImage oldImage(oldPath);
    if (!self) return;
    QMetaObject::invokeMethod(
        self.data(),
        [self, newImage, oldImage] {
          if (self) self->onImagesLoaded(newImage, oldImage);
        },
        Qt::QueuedConnection);
  });
  loader_ = thread;
  thread->start();
}

void WallpaperEngine::skip(const QString& reason) {
  emit skipped(reason);
  emit finished();
  deleteLater();
}

void WallpaperEngine::onImagesLoaded(const QImage& newImage, const QImage& oldImage) {
  pixmapNew_ = QPixmap::fromImage(newImage);
  pixmapOld_ = QPixmap::fromImage(oldImage);

  // If either image fails to load, skip the animation and let the caller commit at once.
  if (pixmapNew_.isNull() || pixmapOld_.isNull()) {
    skip(QStringLiteral("Could not read the wallpaper images"));
    return;
  }

  setWindowOpacity(0.0);
  QString failure;
  const auto attachment = platform_.attach ? platform_.attach(winId(), &failure) : std::nullopt;
  if (!attachment) {
    skip(failure.isEmpty() ? QStringLiteral("Could not attach to WorkerW") : failure);
    return;
  }

  const double dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
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
  setScreenAreas(std::move(areas));

  animated_ = true;
  show();
  QTimer::singleShot(0, this, &WallpaperEngine::startFadeIn);
}

void WallpaperEngine::setScreenAreas(std::vector<ScreenArea> areas) {
  if (!areas.empty()) {
    int minX = areas.front().x;
    int minY = areas.front().y;
    for (const ScreenArea& a : areas) {
      minX = std::min(minX, a.x);
      minY = std::min(minY, a.y);
    }
    int totalW = 0;
    int totalH = 0;
    for (ScreenArea& a : areas) {
      a.x -= minX;
      a.y -= minY;
      totalW = std::max(totalW, a.x + a.w);
      totalH = std::max(totalH, a.y + a.h);
    }
    resize(totalW, totalH);
  }
  areas_ = std::move(areas);
  dpr_ =
      !areas_.empty() ? areas_.front().dpr : (devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0);
  scaledNew_ = layoutForScreens(pixmapNew_, areas_, dpr_, settings_, size());
  scaledOld_ = layoutForScreens(pixmapOld_, areas_, dpr_, settings_, size());
}

void WallpaperEngine::startFadeIn() {
  fadeTimeline_ = new QTimeLine(kFadeInMs, this);
  fadeTimeline_->setUpdateInterval(kFrameMs);
  connect(fadeTimeline_, &QTimeLine::valueChanged, this,
          [this](qreal value) { setWindowOpacity(value); });
  connect(fadeTimeline_, &QTimeLine::finished, this, [this] {
    setWindowOpacity(1.0);
    timeline_->start();
  });
  fadeTimeline_->start();
}

void WallpaperEngine::setProgress(double value) {
  progress_ = value;
  update();
}

void WallpaperEngine::onTimelineFinished() {
  progress_ = 1.0;
  update();
  if (!committed_) {
    committed_ = true;
    emit finished();
  }
}

void WallpaperEngine::paintEvent(QPaintEvent*) {
  try {
    QPainter painter(this);
    const double t = progress_;
    for (size_t i = 0; i < areas_.size(); ++i) {
      const ScreenArea& a = areas_[i];
      const QRectF monitor(a.x, a.y, a.w, a.h);
      const QPainterPath clip = revealPath(animation_, a.x, a.y, a.w, a.h, t);

      if (t < 1.0) {
        painter.save();
        painter.setClipRect(monitor);
        painter.fillRect(monitor, settings_.background);
        if (i < scaledOld_.size()) {
          const ScaledImage& old = scaledOld_[i];
          painter.drawPixmap(QPointF(a.x + old.offsetX / dpr_, a.y + old.offsetY / dpr_),
                             old.pixmap);
        }
        painter.restore();
      }

      if (i >= scaledNew_.size()) continue;
      const ScaledImage& fresh = scaledNew_[i];
      painter.save();
      painter.setClipPath(clip, Qt::IntersectClip);
      painter.setClipRect(monitor, Qt::IntersectClip);
      painter.fillRect(monitor, settings_.background);
      painter.drawPixmap(QPointF(a.x + fresh.offsetX / dpr_, a.y + fresh.offsetY / dpr_),
                         fresh.pixmap);
      painter.restore();
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Wallpaper overlay paint failed:" << error.what();
  }
}

}  // namespace yasb::widgets

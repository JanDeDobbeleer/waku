#include "features/taskbar/taskbar_icons.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPointer>
#include <QRunnable>
#include <QTimer>
#include <algorithm>

namespace wabar::taskbar {

TaskbarIcons::TaskbarIcons(std::shared_ptr<WindowSystem> system, bool async, QObject* parent)
    : QObject(parent), system_(std::move(system)), async_(async) {
  // One loader thread, like IconService: windows are asked one at a time and a hung one costs
  // at most the WM_GETICON timeout.
  pool_.setMaxThreadCount(1);
}

TaskbarIcons::~TaskbarIcons() {
  // Drop the queued loads (each can cost a WM_GETICON timeout) and wait only for the running one.
  pool_.clear();
  pool_.waitForDone();
}

QImage TaskbarIcons::toImage(const IconBitmap& bitmap, int physical) {
  if (bitmap.isNull()) return {};
  QImage source(reinterpret_cast<const uchar*>(bitmap.bgra.constData()), bitmap.width,
                bitmap.height, bitmap.width * 4, QImage::Format_ARGB32);
  QImage copy = source.copy();
  if (copy.width() == physical && copy.height() == physical) return copy;
  return copy.scaled(physical, physical, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QPixmap TaskbarIcons::icon(WindowHandle hwnd, int size, qreal dpr) {
  const std::shared_ptr<WindowSystem> system = system_;
  return iconFor(hwnd, size, dpr,
                 [system, hwnd](int physical) { return system->windowIcon(hwnd, physical); });
}

QPixmap TaskbarIcons::pixmapFromBitmap(const IconBitmap& bitmap, int size, qreal dpr) {
  if (size <= 0) return {};
  const int physical = std::max(1, static_cast<int>(size * dpr));
  const QImage image = toImage(bitmap, physical);
  if (image.isNull()) return {};
  QPixmap pixmap = QPixmap::fromImage(image);
  pixmap.setDevicePixelRatio(dpr);
  return pixmap;
}

QPixmap TaskbarIcons::iconFor(WindowHandle hwnd, int size, qreal dpr, const Loader& loader) {
  if (size <= 0) return {};
  const int physical = std::max(1, static_cast<int>(size * dpr));
  const Key key{hwnd, physical};
  auto found = cache_.find(key);
  if (found == cache_.end() || !found->second.loaded) {
    request(key, loader);
    found = cache_.find(key);
    // Async loads arrive later through iconsChanged.
    if (found == cache_.end() || !found->second.loaded) return {};
  }
  if (found->second.image.isNull()) return {};
  QPixmap pixmap = QPixmap::fromImage(found->second.image);
  pixmap.setDevicePixelRatio(dpr);
  return pixmap;
}

void TaskbarIcons::forget(WindowHandle hwnd) {
  for (auto it = cache_.begin(); it != cache_.end();) {
    it = it->first.first == hwnd ? cache_.erase(it) : std::next(it);
  }
  for (auto it = loaders_.begin(); it != loaders_.end();) {
    it = it->first.first == hwnd ? loaders_.erase(it) : std::next(it);
  }
}

void TaskbarIcons::request(const Key& key, const Loader& loader) {
  if (pending_.count(key)) return;
  loaders_[key] = loader;
  const int physical = key.second;
  if (!async_) {
    const IconBitmap bitmap = loader(physical);
    finish(key, toImage(bitmap, physical), bitmap.complete);
    return;
  }
  pending_.insert(key);
  QPointer<TaskbarIcons> self(this);
  pool_.start(QRunnable::create([self, loader, key] {
    IconBitmap bitmap;
    try {
      bitmap = loader(key.second);
    } catch (...) {
    }
    const QImage image = TaskbarIcons::toImage(bitmap, key.second);
    const bool complete = bitmap.complete;
    // Posted to the application, not to `self`: testing a QPointer here and then posting to the
    // raw pointer races with the GUI thread destroying it. `self` is read on the GUI thread only.
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self, key, image, complete] {
          if (self) self->finish(key, image, complete);
        },
        Qt::QueuedConnection);
  }));
}

void TaskbarIcons::finish(const Key& key, const QImage& image, bool complete) {
  pending_.erase(key);
  if (!complete) {
    // The window did not answer in time; ask again a bit later instead of caching a guess.
    QTimer::singleShot(2000, this, [this, key] {
      const auto loader = loaders_.find(key);
      if (loader != loaders_.end()) request(key, loader->second);
    });
    if (image.isNull()) return;
  }
  cache_[key] = Entry{image, true};
  if (async_) emit iconsChanged();
}

namespace {

// Tile assets carry a margin that shell icons do not, so crop to the drawing and pad to a square
// (Python _toast_app_icon: ImageOps.pad on the alpha bounding box).
IconBitmap toastIcon(const QString& path, int size) {
  QImage image(path);
  if (image.isNull() || size <= 0) return {};
  image = image.convertToFormat(QImage::Format_ARGB32);
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(row[x]) == 0) continue;
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  if (right < left || bottom < top) return {};
  const QImage cropped = image.copy(left, top, right - left + 1, bottom - top + 1)
                             .scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QImage canvas(size, size, QImage::Format_ARGB32);
  canvas.fill(Qt::transparent);
  {
    QPainter painter(&canvas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage((size - cropped.width()) / 2, (size - cropped.height()) / 2, cropped);
  }
  IconBitmap bitmap;
  bitmap.width = bitmap.height = size;
  bitmap.bgra = QByteArray(reinterpret_cast<const char*>(canvas.constBits()),
                           static_cast<qsizetype>(canvas.sizeInBytes()));
  return bitmap;
}

}  // namespace

IconBitmap loadPinnedIcon(const PinSupport& pins, const QString& uniqueId,
                          const PinMetadata& metadata, int size) {
  IconBitmap bitmap = pins.pinnedIcon(uniqueId, metadata, size);
  if (!bitmap.isNull()) return bitmap;
  const UniqueIdParts parts = parseUniqueId(uniqueId);
  if (parts.type == QLatin1String("aumid")) {
    // Unpackaged toast senders (Firefox, Windows system toasts) register an AUMID that
    // shell:AppsFolder does not list.
    if (const auto path = pins.toastIconPath(parts.value)) {
      bitmap = toastIcon(*path, size);
      if (!bitmap.isNull()) return bitmap;
    }
  }
  return pins.defaultIcon();
}

}  // namespace wabar::taskbar

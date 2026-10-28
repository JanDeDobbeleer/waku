#include "widgets/taskbar/taskbar_icons.h"

#include <QPointer>
#include <QRunnable>
#include <QTimer>
#include <algorithm>

namespace yasb::widgets {

TaskbarIcons::TaskbarIcons(std::shared_ptr<platform::WindowSystem> system, bool async,
                           QObject* parent)
    : QObject(parent), system_(std::move(system)), async_(async) {
  // One loader thread, like IconService: windows are asked one at a time and a hung one costs
  // at most the WM_GETICON timeout.
  pool_.setMaxThreadCount(1);
}

TaskbarIcons::~TaskbarIcons() { pool_.waitForDone(); }

QImage TaskbarIcons::toImage(const platform::IconBitmap& bitmap, int physical) {
  if (bitmap.isNull()) return {};
  QImage source(reinterpret_cast<const uchar*>(bitmap.bgra.constData()), bitmap.width,
                bitmap.height, bitmap.width * 4, QImage::Format_ARGB32);
  QImage copy = source.copy();
  if (copy.width() == physical && copy.height() == physical) return copy;
  return copy.scaled(physical, physical, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QPixmap TaskbarIcons::icon(platform::WindowHandle hwnd, int size, qreal dpr) {
  if (size <= 0) return {};
  const int physical = std::max(1, static_cast<int>(size * dpr));
  const Key key{hwnd, physical};
  auto found = cache_.find(key);
  if (found == cache_.end() || !found->second.loaded) {
    request(key);
    found = cache_.find(key);
    // Async loads arrive later through iconsChanged.
    if (found == cache_.end() || !found->second.loaded) return {};
  }
  if (found->second.image.isNull()) return {};
  QPixmap pixmap = QPixmap::fromImage(found->second.image);
  pixmap.setDevicePixelRatio(dpr);
  return pixmap;
}

void TaskbarIcons::forget(platform::WindowHandle hwnd) {
  for (auto it = cache_.begin(); it != cache_.end();) {
    it = it->first.first == hwnd ? cache_.erase(it) : std::next(it);
  }
}

void TaskbarIcons::request(const Key& key) {
  if (pending_.count(key)) return;
  const auto [hwnd, physical] = key;
  if (!async_) {
    const platform::IconBitmap bitmap = system_->windowIcon(hwnd, physical);
    finish(key, toImage(bitmap, physical), bitmap.complete);
    return;
  }
  pending_.insert(key);
  QPointer<TaskbarIcons> self(this);
  auto system = system_;
  pool_.start(QRunnable::create([self, system, key] {
    const auto [handle, size] = key;
    platform::IconBitmap bitmap;
    try {
      bitmap = system->windowIcon(handle, size);
    } catch (...) {
    }
    const QImage image = TaskbarIcons::toImage(bitmap, size);
    const bool complete = bitmap.complete;
    if (!self) return;
    QMetaObject::invokeMethod(
        self.data(),
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
    QTimer::singleShot(2000, this, [this, key] { request(key); });
    if (image.isNull()) return;
  }
  cache_[key] = Entry{image, true};
  if (async_) emit iconsChanged();
}

}  // namespace yasb::widgets

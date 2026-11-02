#include "host/qml/window_thumbnail.h"

#include <QMetaObject>
#include <algorithm>
#include <cmath>

#include "kernel/qt/live_desktop.h"

namespace waku::host {

namespace {

ThumbnailBackend* g_testBackend = nullptr;

// The Windows backend has no state; it is never destroyed so items that outlive static
// destruction can still release their handle.
ThumbnailBackend* defaultBackend() {
  static ThumbnailBackend* const backend =
      kernel::liveDesktop() ? makeWin32ThumbnailBackend().release() : nullptr;
  return backend;
}

ThumbnailBackend* currentBackend() {
  if (g_testBackend) return g_testBackend;
  return kernel::liveDesktop() ? defaultBackend() : nullptr;
}

}  // namespace

WindowThumbnail::WindowThumbnail(QQuickItem* parent) : QQuickItem(parent) {
  refresh_.setInterval(1000);
  connect(&refresh_, &QTimer::timeout, this, [this] { sync(true); });
}

WindowThumbnail::~WindowThumbnail() { release(); }

void WindowThumbnail::setBackendForTesting(ThumbnailBackend* backend) { g_testBackend = backend; }

void WindowThumbnail::setHwnd(qlonglong hwnd) {
  if (hwnd_ == hwnd) return;
  hwnd_ = hwnd;
  release();
  emit hwndChanged();
  scheduleSync();
}

void WindowThumbnail::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  emit activeChanged();
  if (active_)
    scheduleSync();
  else
    release();
}

void WindowThumbnail::setFit(Fit fit) {
  if (fit_ == fit) return;
  fit_ = fit;
  emit fitChanged();
  scheduleSync();
}

void WindowThumbnail::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  scheduleSync();
}

void WindowThumbnail::itemChange(ItemChange change, const ItemChangeData& value) {
  QQuickItem::itemChange(change, value);
  switch (change) {
    case ItemSceneChange:
      release();
      watchWindow(value.window);
      scheduleSync();
      break;
    case ItemVisibleHasChanged:
    case ItemOpacityHasChanged:
    case ItemDevicePixelRatioHasChanged:
    case ItemParentHasChanged:
      scheduleSync();
      break;
    default:
      break;
  }
}

void WindowThumbnail::watchWindow(QQuickWindow* window) {
  if (watched_) disconnect(watched_, nullptr, this, nullptr);
  watched_ = window;
  if (!window) return;
  connect(window, &QWindow::visibleChanged, this, [this] { sync(true); });
  // An ancestor that moves or scales does not tell this item; a frame does.
  connect(window, &QQuickWindow::afterAnimating, this, [this] {
    if (handle_ != 0) sync(false);
  });
}

void WindowThumbnail::scheduleSync() {
  if (syncQueued_) return;
  syncQueued_ = true;
  QMetaObject::invokeMethod(
      this,
      [this] {
        syncQueued_ = false;
        sync(true);
      },
      Qt::QueuedConnection);
}

void WindowThumbnail::syncNow() { sync(true); }

qreal WindowThumbnail::effectiveOpacity() const {
  qreal opacity = 1.0;
  for (const QQuickItem* item = this; item; item = item->parentItem()) opacity *= item->opacity();
  return std::clamp(opacity, 0.0, 1.0);
}

void WindowThumbnail::sync(bool querySize) {
  QQuickWindow* const win = window();
  ThumbnailBackend* const backend = currentBackend();
  // The constructor's parenting runs before this class's itemChange exists.
  if (watched_.data() != win) watchWindow(win);
  const bool live =
      backend && hwnd_ != 0 && active_ && win && win->isVisible() && win->winId() != 0;
  if (!live) {
    release();
    return;
  }
  const quintptr destination = static_cast<quintptr>(win->winId());
  if (handle_ != 0 && (registeredIn_ != destination || backend_ != backend)) release();
  if (!refresh_.isActive()) refresh_.start();

  if (handle_ == 0) {
    handle_ = backend->registerThumbnail(destination, static_cast<quintptr>(hwnd_));
    if (handle_ == 0) {
      setSource(QSize());
      setAvailable(false);
      return;
    }
    backend_ = backend;
    registeredIn_ = destination;
    hasProps_ = false;
    querySize = true;
  }

  if (querySize) setSource(backend_->sourceSize(handle_));

  ThumbnailProps props;
  props.destination =
      thumbnailDestination(mapRectToScene(QRectF(0, 0, width(), height())), win->devicePixelRatio(),
                           source_, fit_ == PreserveAspectFit);
  props.visible = isVisible() && !props.destination.isEmpty() && !source_.isEmpty() &&
                  !(win->windowStates() & Qt::WindowMinimized);
  props.opacity = static_cast<quint8>(std::lround(effectiveOpacity() * 255.0));
  if (!hasProps_ || !(props == lastProps_)) {
    if (!backend_->update(handle_, props)) {
      release();  // the source window is gone; the timer registers again if it comes back
      refresh_.start();
      return;
    }
    lastProps_ = props;
    hasProps_ = true;
  }
  setAvailable(!source_.isEmpty());
}

void WindowThumbnail::release() {
  refresh_.stop();
  if (handle_ != 0 && backend_) backend_->unregisterThumbnail(handle_);
  handle_ = 0;
  registeredIn_ = 0;
  hasProps_ = false;
  setSource(QSize());
  setAvailable(false);
}

void WindowThumbnail::setSource(const QSize& size) {
  const QSize value = size.isEmpty() ? QSize(0, 0) : size;
  if (source_ == value) return;
  source_ = value;
  emit sourceSizeChanged();
}

void WindowThumbnail::setAvailable(bool available) {
  if (available_ == available) return;
  available_ = available;
  emit availableChanged();
}

}  // namespace waku::host

#pragma once

#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QThreadPool>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "platform/pin_support.h"
#include "platform/window_system.h"

namespace yasb::widgets {

// Port of the part of IconService the taskbar uses (WindowIcon): icons load on a worker thread and
// the widget shows a blank until `iconsChanged` fires, then asks again. With `async` false the load
// runs inside icon(), which is what the tests use.
class TaskbarIcons final : public QObject {
  Q_OBJECT

 public:
  explicit TaskbarIcons(std::shared_ptr<platform::WindowSystem> system, bool async = true,
                        QObject* parent = nullptr);
  ~TaskbarIcons() override;

  // The window's icon scaled to size*dpr physical pixels, or a null pixmap while it is loading or
  // when the window has none.
  QPixmap icon(platform::WindowHandle hwnd, int size, qreal dpr);
  // The same for any other source (a pinned app): `key` is the cache key, `loader` produces the
  // bitmap for a physical pixel size on the worker thread (or inline when not async) and must be
  // safe to call there.
  using Loader = std::function<platform::IconBitmap(int physicalSize)>;
  QPixmap iconFor(platform::WindowHandle key, int size, qreal dpr, const Loader& loader);
  // A bitmap scaled to size*dpr physical pixels (null for a null bitmap), for sources that load
  // inline.
  static QPixmap pixmapFromBitmap(const platform::IconBitmap& bitmap, int size, qreal dpr);
  // Drops what is cached for a window (it closed, and its handle may be reused).
  void forget(platform::WindowHandle hwnd);

 signals:
  void iconsChanged();

 private:
  using Key = std::pair<platform::WindowHandle, int>;
  struct Entry {
    QImage image;
    bool loaded = false;
  };

  void request(const Key& key, const Loader& loader);
  void finish(const Key& key, const QImage& image, bool complete);
  static QImage toImage(const platform::IconBitmap& bitmap, int physical);

  std::shared_ptr<platform::WindowSystem> system_;
  bool async_;
  QThreadPool pool_;
  std::map<Key, Entry> cache_;
  std::set<Key> pending_;
  std::map<Key, Loader> loaders_;
};

// Python load_icon for a pinned app (AppIcon / FileIcon): the shell icon of the app, else for a
// Store id the icon registered for its toasts, else the generic application icon. Thread safe when
// `pins` is; runs on the icon worker.
platform::IconBitmap loadPinnedIcon(const platform::PinSupport& pins, const QString& uniqueId,
                                    const platform::PinMetadata& metadata, int size);

}  // namespace yasb::widgets

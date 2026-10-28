#pragma once

#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QThreadPool>
#include <map>
#include <memory>
#include <set>
#include <utility>

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

  void request(const Key& key);
  void finish(const Key& key, const QImage& image, bool complete);
  static QImage toImage(const platform::IconBitmap& bitmap, int physical);

  std::shared_ptr<platform::WindowSystem> system_;
  bool async_;
  QThreadPool pool_;
  std::map<Key, Entry> cache_;
  std::set<Key> pending_;
};

}  // namespace yasb::widgets

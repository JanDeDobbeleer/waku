#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <utility>

namespace waku::kernel {

// Runs `fn` on the GUI thread if `guard` is still alive then. Any thread; for worker, pool and
// system callback threads that report back to a QObject that a reload may delete meanwhile.
//
// The post goes to qApp, never to `guard`: testing the QPointer on the calling thread and then
// posting to it races with the GUI thread deleting the object, and postEvent would then write into
// a freed QObject. `guard` is read on the GUI thread only, where it cannot dangle. Without a
// QCoreApplication nothing runs.
template <typename Object, typename Fn>
void postToGui(const QPointer<Object>& guard, Fn fn) {
  QCoreApplication* app = QCoreApplication::instance();
  if (!app) return;
  QMetaObject::invokeMethod(
      app,
      [guard, fn = std::move(fn)]() mutable {
        if (guard) fn();
      },
      Qt::QueuedConnection);
}

}  // namespace waku::kernel

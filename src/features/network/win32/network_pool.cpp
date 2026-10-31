#include "features/network/win32/network_pool.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

namespace wabar::network {

QThreadPool* networkPool() {
  static QPointer<QThreadPool> pool;
  if (!pool) {
    pool = new QThreadPool(QCoreApplication::instance());
    pool->setObjectName(QStringLiteral("network"));
    pool->setMaxThreadCount(2);
  }
  return pool.data();
}

}  // namespace wabar::network

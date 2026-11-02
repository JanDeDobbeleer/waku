#include "kernel/qt/live_desktop.h"

#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace waku::kernel {

namespace {

QMutex& mutex() {
  static QMutex instance;
  return instance;
}

QSet<QString>& disabled() {
  static QSet<QString> instance;
  return instance;
}

}  // namespace

bool liveDesktop() {
  // platformName() is empty without a QGuiApplication; QCoreApplication-only runs are not
  // offscreen.
  return QGuiApplication::platformName() != QStringLiteral("offscreen");
}

bool liveDesktop(const QString& capability) {
  return liveDesktop() && !capabilityDisabled(capability);
}

void setCapabilityDisabled(const QString& capability, bool isDisabled) {
  const QMutexLocker lock(&mutex());
  if (isDisabled)
    disabled().insert(capability);
  else
    disabled().remove(capability);
}

bool capabilityDisabled(const QString& capability) {
  const QMutexLocker lock(&mutex());
  return disabled().contains(capability);
}

}  // namespace waku::kernel

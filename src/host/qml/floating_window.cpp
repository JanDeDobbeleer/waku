#include "host/qml/floating_window.h"

#include <QQuickWindow>
#include <QScreen>
#include <algorithm>

namespace waku::host {

FloatingWindow::FloatingWindow(QObject* parent) : QsWindow(parent) {}

void FloatingWindow::setTitle(const QString& title) {
  if (title_ == title) return;
  title_ = title;
  if (QQuickWindow* window = backingWindow()) window->setTitle(title);
  emit titleChanged();
}

void FloatingWindow::setMinimumSize(const QSize& size) {
  if (minimumSize_ == size) return;
  minimumSize_ = size;
  if (QQuickWindow* window = backingWindow()) window->setMinimumSize(size);
  emit minimumSizeChanged();
}

void FloatingWindow::setMaximumSize(const QSize& size) {
  if (maximumSize_ == size) return;
  maximumSize_ = size;
  if (QQuickWindow* window = backingWindow(); window && size.isValid()) {
    window->setMaximumSize(size);
  }
  emit maximumSizeChanged();
}

void FloatingWindow::applyGeometry() {
  QQuickWindow* window = backingWindow();
  if (!window) return;
  const QSize size(std::max(1, implicitWidth()), std::max(1, implicitHeight()));
  if (placed_) {
    window->resize(size);
    return;
  }
  QScreen* screen = targetScreen();
  if (!screen) return;
  if (window->screen() != screen) window->setScreen(screen);
  const QRect area = screen->availableGeometry();
  window->setGeometry(QRect(area.x() + (area.width() - size.width()) / 2,
                            area.y() + (area.height() - size.height()) / 2, size.width(),
                            size.height()));
}

Qt::WindowFlags FloatingWindow::windowFlags() const { return Qt::Window; }

}  // namespace waku::host

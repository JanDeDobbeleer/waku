#include "features/taskbar/qml/window_control.h"

#include <QDebug>
#include <exception>
#include <utility>

#include "kernel/qt/live_desktop.h"

namespace wabar::taskbar {

WindowControl::WindowControl(QObject* parent)
    : WindowControl(kernel::liveDesktop() ? makeWin32WindowControlBackend() : nullptr,
                    kernel::liveDesktop(), parent) {}

WindowControl::WindowControl(std::shared_ptr<WindowControlBackend> backend, bool live,
                             QObject* parent)
    : QObject(parent), backend_(live ? std::move(backend) : nullptr) {}

WindowControl::~WindowControl() = default;

bool WindowControl::activate(qlonglong hwnd) {
  if (!backend_ || hwnd <= 0) return false;
  try {
    const auto handle = static_cast<std::uintptr_t>(hwnd);
    if (!backend_->isWindow(handle)) return false;
    if (backend_->isIconic(handle)) backend_->restore(handle);
    return backend_->bringToFront(handle);
  } catch (const std::exception& error) {
    qWarning().noquote() << "WindowControl: activate failed:" << error.what();
  } catch (...) {
    qWarning().noquote() << "WindowControl: activate failed";
  }
  return false;
}

qlonglong WindowControl::foreground() const {
  if (!backend_) return 0;
  try {
    return static_cast<qlonglong>(backend_->foreground());
  } catch (...) {
    return 0;
  }
}

bool WindowControl::isWindow(qlonglong hwnd) const {
  if (!backend_ || hwnd <= 0) return false;
  try {
    return backend_->isWindow(static_cast<std::uintptr_t>(hwnd));
  } catch (...) {
    return false;
  }
}

}  // namespace wabar::taskbar

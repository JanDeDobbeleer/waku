#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <memory>

#include "features/taskbar/window_control_backend.h"

namespace waku::taskbar {

// Brings a window to the front by its HWND, for a shell that learns its windows from somewhere
// other than the taskbar tracker (komorebi's state, for example). No state: every call goes
// straight to Win32. GUI thread only; no thread, no timer.
//
// `activate` restores the window when it is minimised, then forces it to the foreground
// (kernel::forceForegroundFocus). Windows may still refuse a foreground change from a process
// that did not get the last input; the taskbar button of the window then flashes and `activate`
// returns false. Every call returns false or 0 without a live desktop (the offscreen platform).
class WindowControl : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit WindowControl(QObject* parent = nullptr);
  // Test seam: a fake backend; `live` false makes the object inert.
  WindowControl(std::shared_ptr<WindowControlBackend> backend, bool live, QObject* parent);
  ~WindowControl() override;

  // True when the window exists and is now the foreground window.
  Q_INVOKABLE bool activate(qlonglong hwnd);
  // The foreground window, 0 when there is none.
  Q_INVOKABLE qlonglong foreground() const;
  Q_INVOKABLE bool isWindow(qlonglong hwnd) const;

 private:
  std::shared_ptr<WindowControlBackend> backend_;  // null when inert
};

}  // namespace waku::taskbar

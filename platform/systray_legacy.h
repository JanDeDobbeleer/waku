#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "platform/systray_host.h"

namespace yasb::platform {

// Port of services/systray/systray_monitor.py, the fallback when the hook DLL cannot be used
// (emulation, unsupported architecture, DLL missing): a hidden topmost "Shell_TrayWnd" window of
// our own that receives the WM_COPYDATA tray messages, publishes them and forwards them to
// Explorer's real tray window.
//
// Threads: `start()` spawns one worker that creates the window, owns it and runs its message
// loop. `stop()` posts WM_CLOSE to the window from the calling thread and joins the worker.
// Nothing here is exercised by the automated tests, because creating the window takes over tray
// messages for the whole desktop session.
class SystrayLegacy : public TrayHost {
  Q_OBJECT
 public:
  explicit SystrayLegacy(QObject* parent = nullptr);
  ~SystrayLegacy() override;

  void start() override;
  void stop() override;
  QString mode() const override { return QStringLiteral("legacy"); }

 private:
  friend struct LegacyAccess;  // the window procedure trampoline in systray_legacy.cpp
  void threadMain();
  std::int64_t handleMessage(void* hwnd, unsigned message, std::uint64_t wParam,
                             std::int64_t lParam);
  std::int64_t handleCopyData(void* hwnd, unsigned message, std::uint64_t wParam,
                              std::int64_t lParam);
  std::int64_t forwardMessage(void* hwnd, unsigned message, std::uint64_t wParam,
                              std::int64_t lParam);

  std::thread thread_;
  std::atomic<void*> hwnd_{nullptr};
  std::atomic<bool> stopping_{false};
  void* realTray_ = nullptr;  // worker thread only
  unsigned taskbarCreated_ = 0;
};

}  // namespace yasb::platform

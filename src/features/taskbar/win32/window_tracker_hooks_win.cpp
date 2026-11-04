#include <windows.h>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <exception>
#include <utility>
#include <vector>

#include "features/taskbar/window_tracker.h"
#include "kernel/win32/winevent_hook.h"

namespace waku::taskbar {
namespace {

// Forwards SHELLHOOK window messages (delivered to the registered Qt window) to the tracker.
class ShellHookFilter final : public QAbstractNativeEventFilter {
 public:
  ShellHookFilter(UINT message, WindowTracker* tracker) : message_(message), tracker_(tracker) {}

  bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
    if (eventType != "windows_generic_MSG" || !message) return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != message_) return false;
    try {
      tracker_->handleShellHook(msg->wParam, static_cast<std::intptr_t>(msg->lParam));
    } catch (...) {
    }
    if (result) *result = 0;
    return true;
  }

 private:
  UINT message_;
  WindowTracker* tracker_;
};

class Win32TrackerHooks final : public TrackerHooks {
 public:
  ~Win32TrackerHooks() override { uninstall(); }

  bool install(WindowHandle shellHookWindow, WindowTracker& tracker) override {
    uninstall();
    HWND hwnd = reinterpret_cast<HWND>(shellHookWindow);
    const UINT message = RegisterWindowMessageW(L"SHELLHOOK");
    if (!message || !RegisterShellHookWindow(hwnd)) {
      qWarning() << "RegisterShellHookWindow failed:" << GetLastError();
      return false;
    }
    shellWindow_ = hwnd;
    filter_ = new ShellHookFilter(message, &tracker);
    if (QCoreApplication* app = QCoreApplication::instance())
      app->installNativeEventFilter(filter_);

    // One range hook per event group; the tracker filters the object and child ids itself.
    const auto hook = [&](unsigned low, unsigned high, const char* what) {
      kernel::WinEventHook handle;
      const bool ok = handle.install(
          low, high, [&tracker](unsigned event, std::uintptr_t hwnd, long idObject, long idChild) {
            if (hwnd)
              tracker.handleWinEvent(event, static_cast<WindowHandle>(hwnd), idObject, idChild);
          });
      if (ok)
        hooks_.push_back(std::move(handle));
      else
        qWarning() << "Failed to set the" << what << "event hooks";
    };
    hook(shell::kEventObjectCloaked, shell::kEventObjectUncloaked, "cloak/uncloak");
    hook(shell::kEventSystemMinimizeStart, shell::kEventSystemMinimizeEnd, "minimize/restore");
    hook(shell::kEventObjectShow, shell::kEventObjectHide, "show/hide");
    return true;
  }

  void uninstall() override {
    hooks_.clear();
    if (filter_) {
      if (QCoreApplication* app = QCoreApplication::instance())
        app->removeNativeEventFilter(filter_);
      delete filter_;
      filter_ = nullptr;
    }
    if (shellWindow_) {
      DeregisterShellHookWindow(shellWindow_);
      shellWindow_ = nullptr;
    }
  }

 private:
  HWND shellWindow_ = nullptr;
  ShellHookFilter* filter_ = nullptr;
  std::vector<kernel::WinEventHook> hooks_;
};

}  // namespace

std::unique_ptr<TrackerHooks> makeWin32TrackerHooks() {
  return std::make_unique<Win32TrackerHooks>();
}

}  // namespace waku::taskbar

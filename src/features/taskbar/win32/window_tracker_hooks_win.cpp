#include <windows.h>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <exception>
#include <vector>

#include "features/taskbar/window_tracker.h"

namespace waku::taskbar {
namespace {

// The WinEvent callback is a bare function pointer, so it reaches the tracker through this. Only
// one Win32 hook set exists at a time (the tracker is a shared service).
WindowTracker* g_tracker = nullptr;

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                           DWORD, DWORD) {
  try {
    if (g_tracker && hwnd)
      g_tracker->handleWinEvent(event, reinterpret_cast<WindowHandle>(hwnd), idObject, idChild);
  } catch (...) {
    // Never unwind into the system.
  }
}

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

    g_tracker = &tracker;
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const auto hook = [&](DWORD low, DWORD high, const char* what) {
      HWINEVENTHOOK handle = SetWinEventHook(low, high, nullptr, winEventProc, 0, 0, flags);
      if (handle)
        hooks_.push_back(handle);
      else
        qWarning() << "Failed to set the" << what << "event hooks";
    };
    hook(shell::kEventObjectCloaked, shell::kEventObjectUncloaked, "cloak/uncloak");
    hook(shell::kEventSystemMinimizeStart, shell::kEventSystemMinimizeEnd, "minimize/restore");
    hook(shell::kEventObjectShow, shell::kEventObjectHide, "show/hide");
    return true;
  }

  void uninstall() override {
    for (HWINEVENTHOOK handle : hooks_) UnhookWinEvent(handle);
    hooks_.clear();
    g_tracker = nullptr;
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
  std::vector<HWINEVENTHOOK> hooks_;
};

}  // namespace

std::unique_ptr<TrackerHooks> makeWin32TrackerHooks() {
  return std::make_unique<Win32TrackerHooks>();
}

}  // namespace waku::taskbar

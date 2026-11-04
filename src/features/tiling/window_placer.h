#pragma once

#include <QRect>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace waku::tiling {

using WindowId = std::uintptr_t;   // an HWND
using MonitorId = std::uintptr_t;  // an HMONITOR

// What the tiler needs to know about one top-level window, read at one moment. The real placer
// fills it from Win32 (win32/window_placer_win.cpp); tests write it by hand.
struct WindowFacts {
  WindowId hwnd = 0;
  bool visible = false;
  bool iconic = false;
  bool cloaked = false;
  bool zoomed = false;          // maximized by the user
  bool fullscreen = false;      // covers its whole monitor
  bool ownedByVisible = false;  // has a visible owner window (dialogs, popups)
  bool toolWindow = false;      // WS_EX_TOOLWINDOW
  bool thickFrame = false;      // WS_THICKFRAME: the user can resize it
  bool caption = false;         // WS_CAPTION
  bool elevated = false;        // its process runs elevated
  bool ownProcess = false;      // a window of waku itself
  QString processName;          // "notepad.exe"; empty when unknown
  QString className;
  QString title;
  MonitorId monitor = 0;  // the monitor with the largest share of the window
  QRect rect;             // the visible bounds (without the invisible resize border)
  QString desktopId;      // the virtual desktop GUID; empty when unknown
};

struct PlaceResult {
  bool ok = false;  // false when the window refused or vanished
  QRect achieved;   // the visible bounds read back after the move
};

// Platform access of the tiler. The tiler never calls Win32 itself, so tests drive it with a fake
// (testing/fake_window_placer.h) and never touch a real window. GUI thread only.
class WindowPlacer {
 public:
  virtual ~WindowPlacer() = default;

  // The top-level windows that exist now (a superset of the tileable ones).
  virtual std::vector<WindowId> candidates() = 0;
  // Nothing when the window is gone.
  virtual std::optional<WindowFacts> query(WindowId hwnd) = 0;
  // The work area of a monitor in physical pixels; empty when the monitor is unknown.
  virtual QRect workArea(MonitorId monitor) = 0;
  // The GUID of the virtual desktop that is shown; empty when Windows does not say.
  virtual QString currentDesktop() = 0;
  virtual std::optional<WindowId> foreground() = 0;
  // Moves and sizes the window so that its visible bounds are `slot`, without activating it, and
  // reads the bounds back. The real one adds the invisible border (see layouts.h, outerRect).
  virtual PlaceResult place(WindowId hwnd, const QRect& slot) = 0;
  virtual void activate(WindowId hwnd) = 0;
  // Brings the window to the front of the z-order without activating it or moving it (the top of
  // a stack goes over the windows that share its rectangle).
  virtual void raise(WindowId hwnd) = 0;
  // Asks the window to close like its close button (WM_CLOSE); it may refuse or ask to save.
  virtual bool close(WindowId hwnd) = 0;
  // Minimizes the window.
  virtual bool minimize(WindowId hwnd) = 0;
};

// The real placer (win32/window_placer_win.cpp).
std::unique_ptr<WindowPlacer> makeWin32WindowPlacer();

// True when waku itself runs elevated; a window of an elevated process cannot be moved from a
// normal one (UIPI). Defined in win32/window_placer_win.cpp.
bool processIsElevated();

}  // namespace waku::tiling

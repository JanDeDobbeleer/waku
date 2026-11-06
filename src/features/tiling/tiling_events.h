#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "features/tiling/window_placer.h"

namespace waku::tiling {

// What happened to a window, as far as the tiler cares.
enum class EventKind {
  Show,           // EVENT_OBJECT_SHOW
  Destroy,        // EVENT_OBJECT_DESTROY
  Hide,           // EVENT_OBJECT_HIDE
  NameChange,     // EVENT_OBJECT_NAMECHANGE
  Cloaked,        // EVENT_OBJECT_CLOAKED
  Uncloaked,      // EVENT_OBJECT_UNCLOAKED
  MinimizeStart,  // EVENT_SYSTEM_MINIMIZESTART
  MinimizeEnd,    // EVENT_SYSTEM_MINIMIZEEND
  MoveSizeStart,  // EVENT_SYSTEM_MOVESIZESTART
  MoveSizeEnd,    // EVENT_SYSTEM_MOVESIZEEND
  Foreground,     // EVENT_SYSTEM_FOREGROUND
};

struct WindowEvent {
  EventKind kind = EventKind::Show;
  WindowId hwnd = 0;
};

// The WinEvent codes (winuser.h), repeated here so the root stays free of the Windows SDK.
namespace winevent {
constexpr unsigned kForeground = 0x0003;
constexpr unsigned kMoveSizeStart = 0x000A;
constexpr unsigned kMoveSizeEnd = 0x000B;
constexpr unsigned kMinimizeStart = 0x0016;
constexpr unsigned kMinimizeEnd = 0x0017;
constexpr unsigned kObjectDestroy = 0x8001;
constexpr unsigned kObjectShow = 0x8002;
constexpr unsigned kObjectHide = 0x8003;
constexpr unsigned kObjectNameChange = 0x800C;
constexpr unsigned kObjectCloaked = 0x8017;
constexpr unsigned kObjectUncloaked = 0x8018;
}  // namespace winevent

// The kind of a WinEvent code; nothing for a code the tiler does not use.
inline std::optional<EventKind> eventKindFromWinEvent(unsigned code) {
  switch (code) {
    case winevent::kObjectShow:
      return EventKind::Show;
    case winevent::kObjectDestroy:
      return EventKind::Destroy;
    case winevent::kObjectHide:
      return EventKind::Hide;
    case winevent::kObjectNameChange:
      return EventKind::NameChange;
    case winevent::kObjectCloaked:
      return EventKind::Cloaked;
    case winevent::kObjectUncloaked:
      return EventKind::Uncloaked;
    case winevent::kMinimizeStart:
      return EventKind::MinimizeStart;
    case winevent::kMinimizeEnd:
      return EventKind::MinimizeEnd;
    case winevent::kMoveSizeStart:
      return EventKind::MoveSizeStart;
    case winevent::kMoveSizeEnd:
      return EventKind::MoveSizeEnd;
    case winevent::kForeground:
      return EventKind::Foreground;
    default:
      return std::nullopt;
  }
}

// The source of window events. The real one (win32/tiling_hooks_win.cpp) installs out-of-context
// WinEvent hooks, one range per event group, and delivers only events of real top-level windows
// on the GUI thread. Absent in tests, which call TilingEngine::handle directly.
class TilingHooks {
 public:
  using Sink = std::function<void(const WindowEvent&)>;
  virtual ~TilingHooks() = default;
  // False when no hook could be installed.
  virtual bool install(Sink sink) = 0;
  virtual void uninstall() = 0;
};

std::unique_ptr<TilingHooks> makeWin32TilingHooks();

}  // namespace waku::tiling

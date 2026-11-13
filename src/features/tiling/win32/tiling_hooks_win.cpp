// The WinEvent hooks of the tiler: documented SetWinEventHook only (through kernel::WinEventHook),
// out of context, so waku is never injected into another process and the callbacks run on the GUI
// thread. One range hook per event group. Location changes are hooked only for an active managed
// mainStack resize and are filtered to that window.
#include <windows.h>

#include <utility>
#include <vector>

#include "features/tiling/tiling_events.h"
#include "kernel/win32/winevent_hook.h"

namespace waku::tiling {
namespace {

class Win32TilingHooks final : public TilingHooks {
 public:
  ~Win32TilingHooks() override { uninstall(); }

  bool install(Sink sink) override {
    uninstall();
    sink_ = std::move(sink);
    const auto hook = [this](unsigned low, unsigned high) {
      kernel::WinEventHook handle;
      if (!handle.install(low, high,
                          [this](unsigned event, std::uintptr_t hwnd, long idObject, long idChild) {
                            deliver(event, hwnd, idObject, idChild);
                          })) {
        return;
      }
      hooks_.push_back(std::move(handle));
    };
    hook(winevent::kObjectDestroy, winevent::kObjectHide);  // destroy, show, hide
    hook(winevent::kObjectNameChange, winevent::kObjectNameChange);
    hook(winevent::kObjectCloaked, winevent::kObjectUncloaked);
    hook(winevent::kMinimizeStart, winevent::kMinimizeEnd);
    hook(winevent::kMoveSizeStart, winevent::kMoveSizeEnd);
    hook(winevent::kForeground, winevent::kForeground);
    if (hooks_.empty()) sink_ = nullptr;
    return !hooks_.empty();
  }

  void trackLocation(WindowId hwnd, bool enabled) override {
    if (!enabled) {
      if (trackedLocation_ == hwnd || hwnd == 0) {
        locationHook_.uninstall();
        trackedLocation_ = 0;
      }
      return;
    }
    if (trackedLocation_ == hwnd) return;
    locationHook_.uninstall();
    trackedLocation_ = 0;
    if (locationHook_.install(winevent::kObjectLocationChange, winevent::kObjectLocationChange,
                              [this](unsigned event, std::uintptr_t id, long idObject,
                                     long idChild) { deliver(event, id, idObject, idChild); })) {
      trackedLocation_ = hwnd;
    }
  }

  void uninstall() override {
    hooks_.clear();
    locationHook_.uninstall();
    trackedLocation_ = 0;
    sink_ = nullptr;
  }

 private:
  void deliver(unsigned event, std::uintptr_t id, long idObject, long idChild) {
    if (!sink_ || id == 0 || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    const std::optional<EventKind> kind = eventKindFromWinEvent(event);
    if (!kind) return;
    if (*kind == EventKind::LocationChange && id != trackedLocation_) return;
    // A destroyed window has no ancestor any more; every other event must come from a top-level
    // window, not a child control.
    if (*kind != EventKind::Destroy) {
      HWND hwnd = reinterpret_cast<HWND>(id);
      if (GetAncestor(hwnd, GA_ROOT) != hwnd) return;
    }
    sink_(WindowEvent{*kind, id});
  }

  Sink sink_;
  std::vector<kernel::WinEventHook> hooks_;
  kernel::WinEventHook locationHook_;
  std::uintptr_t trackedLocation_ = 0;
};

}  // namespace

std::unique_ptr<TilingHooks> makeWin32TilingHooks() { return std::make_unique<Win32TilingHooks>(); }

}  // namespace waku::tiling

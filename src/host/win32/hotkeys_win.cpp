#include <windows.h>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <set>

#include "host/hotkeys.h"

namespace wabar::host {
namespace {

// RegisterHotKey (documented) with a null window: WM_HOTKEY is posted to the registering thread's
// queue, which is the GUI thread, and Qt's message loop hands every message to native event
// filters. Owner: the Hotkeys singleton; every id is unregistered by unregisterKey() or, at the
// latest, the destructor.
class Win32HotkeyBackend final : public HotkeyBackend, public QAbstractNativeEventFilter {
 public:
  ~Win32HotkeyBackend() override {
    for (const int id : ids_) UnregisterHotKey(nullptr, id);
    ids_.clear();
    removeFilter();
  }

  bool registerKey(const HotkeyCombo& combo, int id) override {
    constexpr unsigned kNoRepeat = 0x4000;  // MOD_NOREPEAT
    if (!RegisterHotKey(nullptr, id, combo.modifiers | kNoRepeat, combo.virtualKey)) return false;
    if (ids_.empty() && QCoreApplication::instance()) {
      QCoreApplication::instance()->installNativeEventFilter(this);
      installed_ = true;
    }
    ids_.insert(id);
    return true;
  }

  void unregisterKey(int id) override {
    if (ids_.erase(id) == 0) return;
    UnregisterHotKey(nullptr, id);
    if (ids_.empty()) removeFilter();
  }

  bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
    if (ids_.empty() || eventType != "windows_generic_MSG" || !message) return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;
    const int id = static_cast<int>(msg->wParam);
    if (!ids_.contains(id)) return false;
    emitPressed(id);
    return true;
  }

 private:
  void removeFilter() {
    if (installed_ && QCoreApplication::instance()) {
      QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    installed_ = false;
  }

  std::set<int> ids_;
  bool installed_ = false;
};

}  // namespace

std::unique_ptr<HotkeyBackend> makeWin32HotkeyBackend() {
  return std::make_unique<Win32HotkeyBackend>();
}

}  // namespace wabar::host

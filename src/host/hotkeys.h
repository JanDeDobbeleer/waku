#pragma once

#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace wabar::host {

// A global hotkey as RegisterHotKey wants it: MOD_ALT 1, MOD_CONTROL 2, MOD_SHIFT 4, MOD_WIN 8.
struct HotkeyCombo {
  unsigned modifiers = 0;
  unsigned virtualKey = 0;
  bool operator==(const HotkeyCombo& other) const {
    return modifiers == other.modifiers && virtualKey == other.virtualKey;
  }
  bool operator<(const HotkeyCombo& other) const {
    return modifiers != other.modifiers ? modifiers < other.modifiers
                                        : virtualKey < other.virtualKey;
  }
};
// "alt+e", "ctrl+shift+f5", "win+space": modifiers alt/ctrl/control/shift/win/windows/super/meta
// plus one letter, digit, F1..F24 or space. Case and spaces do not matter. nullopt otherwise.
std::optional<HotkeyCombo> parseHotkey(const QString& keys);

// Where the keys live. The real one calls RegisterHotKey on the GUI thread and receives WM_HOTKEY
// through a native event filter in Qt's own message loop; tests replace it with a fake
// (host/testing/hotkeys_fixtures.h). Many ids, one per distinct combination.
class HotkeyBackend {
 public:
  virtual ~HotkeyBackend() = default;
  // False when the combination is taken or the call fails.
  virtual bool registerKey(const HotkeyCombo& combo, int id) = 0;
  virtual void unregisterKey(int id) = 0;
  // Called on the GUI thread with the id of a key that was pressed.
  void setPressedHandler(std::function<void(int)> handler) { pressed_ = std::move(handler); }

 protected:
  void emitPressed(int id) {
    if (pressed_) pressed_(id);
  }

 private:
  std::function<void(int)> pressed_;
};

// The real backend (win32/hotkeys_win.cpp).
std::unique_ptr<HotkeyBackend> makeWin32HotkeyBackend();

// The hotkeys of a run: shortcuts add a combination with a callback and remove it by the handle.
// Two shortcuts on one combination share one registration and both fire. The backend is built on
// the first add: the real one when kernel::liveDesktop("hotkeys") is true, otherwise none, and
// then every add fails quietly (`active()` is false).
//
// Threading: GUI thread only. Process-wide state, so one instance (see docs/architecture.md).
class Hotkeys {
 public:
  using Handle = int;
  static Hotkeys& instance();
  ~Hotkeys();

  // True when a backend exists (or would be built): false on a desktop that is not live.
  bool active();
  // A handle > 0, or 0 when the combination could not be registered. A refused combination is
  // logged once.
  Handle add(const HotkeyCombo& combo, std::function<void()> pressed);
  // Unknown handles are ignored. The combination is released with its last handle.
  void remove(Handle handle);

  // Tests: replace the backend (nullptr: inert) and drop every registration.
  void setBackendForTests(std::unique_ptr<HotkeyBackend> backend);
  // Tests: forget the backend so the next add builds the default one again.
  void resetForTests();

 private:
  Hotkeys() = default;
  struct Slot {
    int backendId = 0;
    std::vector<Handle> handles;
  };
  void resolveBackend();
  void dispatch(int backendId);
  void clear();

  std::unique_ptr<HotkeyBackend> backend_;
  bool resolved_ = false;
  int nextBackendId_ = 0x2000;
  Handle nextHandle_ = 1;
  std::map<HotkeyCombo, Slot> slots_;
  std::map<Handle, std::pair<HotkeyCombo, std::function<void()>>> handles_;
  std::set<HotkeyCombo> reported_;
};

}  // namespace wabar::host

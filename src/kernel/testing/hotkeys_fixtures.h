#pragma once

// Header-only fakes for tests (rule S5: only tests include testing/). A fake hotkey backend never
// calls the Windows API, so a test never takes a real key on the user's machine.

#include <map>
#include <memory>
#include <set>

#include "kernel/qt/hotkeys.h"

namespace wabar::kernel {

class FakeHotkeyBackend final : public HotkeyBackend {
 public:
  bool registerKey(const HotkeyCombo& combo, int id) override {
    if (taken.contains(combo)) return false;
    registered[id] = combo;
    ++registerCalls;
    return true;
  }
  void unregisterKey(int id) override {
    registered.erase(id);
    ++unregisterCalls;
  }
  // The user presses `combo`: every registered id on it fires.
  bool press(const HotkeyCombo& combo) {
    for (const auto& [id, registeredCombo] : registered) {
      if (registeredCombo == combo) {
        emitPressed(id);
        return true;
      }
    }
    return false;
  }

  std::set<HotkeyCombo> taken;  // combinations "another program" owns
  std::map<int, HotkeyCombo> registered;
  int registerCalls = 0;
  int unregisterCalls = 0;
};

// Installs a fake backend in the Hotkeys singleton for one test and restores the default after.
// Make it first, so the shortcuts die before it does.
class ScopedFakeHotkeys {
 public:
  ScopedFakeHotkeys() {
    auto fake = std::make_unique<FakeHotkeyBackend>();
    backend = fake.get();
    Hotkeys::instance().setBackendForTests(std::move(fake));
  }
  ~ScopedFakeHotkeys() { Hotkeys::instance().resetForTests(); }
  ScopedFakeHotkeys(const ScopedFakeHotkeys&) = delete;
  ScopedFakeHotkeys& operator=(const ScopedFakeHotkeys&) = delete;

  FakeHotkeyBackend* backend = nullptr;
};

}  // namespace wabar::kernel

#include "kernel/qt/hotkeys.h"

#include <QDebug>
#include <QStringList>
#include <exception>

#include "kernel/qt/live_desktop.h"

namespace wabar::kernel {

std::optional<HotkeyCombo> parseHotkey(const QString& keys) {
  HotkeyCombo combo;
  for (QString part : keys.split(QLatin1Char('+'))) {
    part = part.simplified().remove(QLatin1Char(' ')).toLower();
    if (part.isEmpty()) continue;
    if (part == QLatin1String("alt")) {
      combo.modifiers |= 1;
    } else if (part == QLatin1String("ctrl") || part == QLatin1String("control")) {
      combo.modifiers |= 2;
    } else if (part == QLatin1String("shift")) {
      combo.modifiers |= 4;
    } else if (part == QLatin1String("win") || part == QLatin1String("windows") ||
               part == QLatin1String("super") || part == QLatin1String("meta")) {
      combo.modifiers |= 8;
    } else {
      if (combo.virtualKey != 0) return std::nullopt;
      if (part.size() == 1 && ((part[0] >= QLatin1Char('a') && part[0] <= QLatin1Char('z')) ||
                               (part[0] >= QLatin1Char('0') && part[0] <= QLatin1Char('9')))) {
        combo.virtualKey = static_cast<unsigned>(part[0].toUpper().unicode());
      } else if (part == QLatin1String("space")) {
        combo.virtualKey = 0x20u /* VK_SPACE */;
      } else if (part.size() >= 2 && part[0] == QLatin1Char('f')) {
        bool ok = false;
        const int number = part.mid(1).toInt(&ok);
        if (!ok || number < 1 || number > 24) return std::nullopt;
        combo.virtualKey = static_cast<unsigned>(0x70 /* VK_F1 */ + number - 1);
      } else {
        return std::nullopt;
      }
    }
  }
  if (combo.virtualKey == 0) return std::nullopt;
  return combo;
}

Hotkeys& Hotkeys::instance() {
  static Hotkeys hotkeys;
  return hotkeys;
}

Hotkeys::~Hotkeys() { clear(); }

void Hotkeys::resolveBackend() {
  if (resolved_) return;
  resolved_ = true;
  if (!liveDesktop(QStringLiteral("hotkeys"))) return;
  backend_ = makeWin32HotkeyBackend();
  if (backend_) backend_->setPressedHandler([this](int id) { dispatch(id); });
}

bool Hotkeys::active() {
  resolveBackend();
  return backend_ != nullptr;
}

Hotkeys::Handle Hotkeys::add(const HotkeyCombo& combo, std::function<void()> pressed) {
  resolveBackend();
  if (!backend_ || !pressed) return 0;
  auto slot = slots_.find(combo);
  if (slot == slots_.end()) {
    const int id = nextBackendId_;
    if (!backend_->registerKey(combo, id)) {
      if (reported_.insert(combo).second) {
        qWarning().noquote() << "Hotkeys: the key combination (modifiers" << combo.modifiers
                             << "key" << combo.virtualKey << ") is taken by another program";
      }
      return 0;
    }
    ++nextBackendId_;
    slot = slots_.emplace(combo, Slot{id, {}}).first;
  }
  const Handle handle = nextHandle_++;
  slot->second.handles.push_back(handle);
  handles_.emplace(handle, std::make_pair(combo, std::move(pressed)));
  return handle;
}

void Hotkeys::remove(Handle handle) {
  const auto found = handles_.find(handle);
  if (found == handles_.end()) return;
  const HotkeyCombo combo = found->second.first;
  handles_.erase(found);
  const auto slot = slots_.find(combo);
  if (slot == slots_.end()) return;
  std::erase(slot->second.handles, handle);
  if (slot->second.handles.empty()) {
    if (backend_) backend_->unregisterKey(slot->second.backendId);
    slots_.erase(slot);
  }
}

void Hotkeys::dispatch(int backendId) {
  for (const auto& [combo, slot] : slots_) {
    if (slot.backendId != backendId) continue;
    // Copied: a callback may remove its own shortcut.
    const std::vector<Handle> handles = slot.handles;
    for (const Handle handle : handles) {
      const auto found = handles_.find(handle);
      if (found == handles_.end()) continue;
      const std::function<void()> callback = found->second.second;
      try {
        callback();
      } catch (const std::exception& error) {
        qWarning().noquote() << "Hotkeys: shortcut failed:" << error.what();
      } catch (...) {
        qWarning().noquote() << "Hotkeys: shortcut failed";
      }
    }
    return;
  }
}

void Hotkeys::clear() {
  if (backend_) {
    for (const auto& [combo, slot] : slots_) backend_->unregisterKey(slot.backendId);
  }
  slots_.clear();
  handles_.clear();
}

void Hotkeys::setBackendForTests(std::unique_ptr<HotkeyBackend> backend) {
  clear();
  reported_.clear();
  backend_ = std::move(backend);
  if (backend_) backend_->setPressedHandler([this](int id) { dispatch(id); });
  resolved_ = true;
}

void Hotkeys::resetForTests() {
  clear();
  reported_.clear();
  backend_.reset();
  resolved_ = false;
}

}  // namespace wabar::kernel

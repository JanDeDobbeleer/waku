#include "host/qml/global_shortcut.h"

#include <QDebug>
#include <QPointer>
#include <algorithm>
#include <vector>

namespace waku::host {
namespace {

// The shortcuts of the process, for `wakuc shortcut <name>`. GUI thread only.
std::vector<GlobalShortcut*>& liveShortcuts() {
  static std::vector<GlobalShortcut*> shortcuts;
  return shortcuts;
}

}  // namespace

GlobalShortcut::GlobalShortcut(QObject* parent) : QObject(parent) {}

GlobalShortcut::~GlobalShortcut() {
  release();
  std::erase(liveShortcuts(), this);
}

void GlobalShortcut::setName(const QString& name) {
  if (name_ == name) return;
  name_ = name;
  emit nameChanged();
}

void GlobalShortcut::setAppid(const QString& appid) {
  if (appid_ == appid) return;
  appid_ = appid;
  emit appidChanged();
}

void GlobalShortcut::setDescription(const QString& description) {
  if (description_ == description) return;
  description_ = description;
  emit descriptionChanged();
}

void GlobalShortcut::setKeys(const QString& keys) {
  if (keys_ == keys) return;
  keys_ = keys;
  emit keysChanged();
  if (complete_) applyKeys();
}

void GlobalShortcut::componentComplete() {
  complete_ = true;
  liveShortcuts().push_back(this);
  applyKeys();
}

void GlobalShortcut::release() {
  if (handle_ == 0) return;
  kernel::Hotkeys::instance().remove(handle_);
  handle_ = 0;
}

void GlobalShortcut::applyKeys() {
  const bool was = isRegistered();
  release();
  if (!keys_.trimmed().isEmpty()) {
    const std::optional<kernel::HotkeyCombo> combo = kernel::parseHotkey(keys_);
    if (!combo) {
      qWarning().noquote() << "GlobalShortcut" << name_ << ": cannot read keys" << keys_;
    } else {
      // The callback never outlives this object: the destructor removes the handle.
      handle_ = kernel::Hotkeys::instance().add(*combo, [this] { fire(); });
    }
  }
  if (was != isRegistered()) emit registeredChanged();
}

void GlobalShortcut::fire() {
  // A handler may destroy this shortcut (a shell reload, a Loader): stop at the first sign of it.
  const QPointer<GlobalShortcut> self(this);
  pressed_ = true;
  emit pressedChanged();
  if (!self) return;
  emit pressed();
  if (!self) return;
  pressed_ = false;
  emit pressedChanged();
  if (!self) return;
  emit released();
}

bool GlobalShortcut::fireByName(const QString& name) {
  if (name.isEmpty()) return false;
  // Copied: a handler may destroy or create shortcuts (a shell reload).
  const std::vector<GlobalShortcut*> shortcuts = liveShortcuts();
  bool fired = false;
  for (GlobalShortcut* shortcut : shortcuts) {
    // A handler may have destroyed this one: look it up before touching it.
    if (std::ranges::find(liveShortcuts(), shortcut) != liveShortcuts().end() &&
        !shortcut->name_.isEmpty() && shortcut->name_ == name) {
      shortcut->fire();
      fired = true;
    }
  }
  return fired;
}

QStringList GlobalShortcut::names() {
  QStringList result;
  for (const GlobalShortcut* shortcut : liveShortcuts()) {
    if (!shortcut->name_.isEmpty()) result << shortcut->name_;
  }
  result.sort();
  result.removeDuplicates();
  return result;
}

}  // namespace waku::host

#include "features/tiling/tiling_model.h"

#include <algorithm>

namespace waku::tiling {

void TilingModel::add(const WorkspaceKey& key, WindowId hwnd) {
  remove(hwnd);
  floating_.erase(hwnd);
  stacks_[key].push_back(Slot{{hwnd}, 0});
  owner_[hwnd] = key;
}

TilingModel::Where TilingModel::find(WindowId hwnd) {
  const auto owner = owner_.find(hwnd);
  if (owner == owner_.end()) return {};
  const auto stack = stacks_.find(owner->second);
  if (stack == stacks_.end()) return {};
  for (size_t s = 0; s < stack->second.size(); ++s) {
    const std::vector<WindowId>& windows = stack->second[s].windows;
    const auto at = std::find(windows.begin(), windows.end(), hwnd);
    if (at != windows.end()) return {&stack->second, s, static_cast<size_t>(at - windows.begin())};
  }
  return {};
}

void TilingModel::setTop(const WorkspaceKey& key, Slot& slot, size_t index) {
  if (index == slot.top) return;
  const WindowId from = slot.topWindow();
  slot.top = index;
  const WindowId to = slot.topWindow();
  const auto view = views_.find(key);
  if (view == views_.end()) return;
  const auto weight = view->second.weights.find(from);
  if (weight != view->second.weights.end()) {
    view->second.weights[to] = weight->second;
    view->second.weights.erase(from);
  }
  if (view->second.monocle == from) view->second.monocle = to;
}

std::optional<WorkspaceKey> TilingModel::remove(WindowId hwnd) {
  const auto found = owner_.find(hwnd);
  if (found == owner_.end()) return std::nullopt;
  const WorkspaceKey key = found->second;
  const Where where = find(hwnd);
  owner_.erase(found);
  if (where.list) {
    Slot& slot = (*where.list)[where.slot];
    if (slot.windows.size() > 1) {
      // Another window of the stack takes over the slot: the next one down, else the last.
      if (slot.windows[slot.top] == hwnd) {
        const size_t next = std::min(where.index, slot.windows.size() - 2);
        slot.windows.erase(slot.windows.begin() + static_cast<std::ptrdiff_t>(where.index));
        slot.top = next;
        // The weight goes with the slot; the monocle mark does not outlive its window.
        const auto view = views_.find(key);
        if (view != views_.end()) {
          const auto weight = view->second.weights.find(hwnd);
          if (weight != view->second.weights.end()) {
            view->second.weights[slot.topWindow()] = weight->second;
          }
        }
      } else {
        slot.windows.erase(slot.windows.begin() + static_cast<std::ptrdiff_t>(where.index));
        if (where.index < slot.top) --slot.top;
      }
    } else {
      where.list->erase(where.list->begin() + static_cast<std::ptrdiff_t>(where.slot));
      if (where.list->empty()) stacks_.erase(key);
    }
  }
  const auto view = views_.find(key);
  if (view != views_.end()) {
    view->second.weights.erase(hwnd);
    if (view->second.monocle == hwnd) view->second.monocle = 0;
  }
  return key;
}

std::optional<WorkspaceKey> TilingModel::keyOf(WindowId hwnd) const {
  const auto found = owner_.find(hwnd);
  if (found == owner_.end()) return std::nullopt;
  return found->second;
}

std::vector<WindowId> TilingModel::stack(const WorkspaceKey& key) const {
  std::vector<WindowId> out;
  for (const Slot& slot : slotList(key)) out.push_back(slot.topWindow());
  return out;
}

const std::vector<Slot>& TilingModel::slotList(const WorkspaceKey& key) const {
  static const std::vector<Slot> none;
  const auto found = stacks_.find(key);
  return found == stacks_.end() ? none : found->second;
}

std::vector<WindowId> TilingModel::members(const WorkspaceKey& key) const {
  std::vector<WindowId> out;
  for (const Slot& slot : slotList(key))
    out.insert(out.end(), slot.windows.begin(), slot.windows.end());
  return out;
}

std::vector<WindowId> TilingModel::stackOf(WindowId hwnd) const {
  const std::optional<WorkspaceKey> key = keyOf(hwnd);
  if (!key) return {};
  for (const Slot& slot : slotList(*key)) {
    if (std::find(slot.windows.begin(), slot.windows.end(), hwnd) != slot.windows.end()) {
      return slot.windows;
    }
  }
  return {};
}

int TilingModel::stackCount() const {
  int count = 0;
  for (const auto& entry : stacks_) {
    for (const Slot& slot : entry.second) count += slot.windows.size() > 1 ? 1 : 0;
  }
  return count;
}

bool TilingModel::isTop(WindowId hwnd) const {
  const std::optional<WorkspaceKey> key = keyOf(hwnd);
  if (!key) return false;
  for (const Slot& slot : slotList(*key)) {
    if (slot.topWindow() == hwnd) return true;
  }
  return false;
}

std::vector<WorkspaceKey> TilingModel::keys() const {
  std::vector<WorkspaceKey> out;
  out.reserve(stacks_.size());
  for (const auto& entry : stacks_) out.push_back(entry.first);
  return out;
}

void TilingModel::setFloating(WindowId hwnd, bool floating) {
  if (floating) {
    remove(hwnd);
    floating_.insert(hwnd);
  } else {
    floating_.erase(hwnd);
  }
}

void TilingModel::forget(WindowId hwnd) {
  remove(hwnd);
  floating_.erase(hwnd);
}

QString TilingModel::layout(const WorkspaceKey& key, const QString& fallback) const {
  const auto found = layouts_.find(key);
  return found == layouts_.end() ? fallback : found->second;
}

void TilingModel::setLayout(const WorkspaceKey& key, const QString& name) { layouts_[key] = name; }

const WorkspaceView& TilingModel::view(const WorkspaceKey& key) const {
  static const WorkspaceView defaults;
  const auto found = views_.find(key);
  return found == views_.end() ? defaults : found->second;
}

void TilingModel::clearRatios() {
  for (auto& entry : views_) entry.second.ratio.reset();
}

bool TilingModel::swap(WindowId a, WindowId b) {
  const std::optional<WorkspaceKey> key = keyOf(a);
  if (!key || key != keyOf(b) || a == b) return false;
  const Where wa = find(a);
  const Where wb = find(b);
  if (!wa.list || !wb.list || wa.slot == wb.slot) return false;
  std::iter_swap(wa.list->begin() + static_cast<std::ptrdiff_t>(wa.slot),
                 wa.list->begin() + static_cast<std::ptrdiff_t>(wb.slot));
  return true;
}

bool TilingModel::promote(WindowId hwnd) {
  const Where where = find(hwnd);
  if (!where.list || where.slot == 0) return false;
  std::rotate(where.list->begin(), where.list->begin() + static_cast<std::ptrdiff_t>(where.slot),
              where.list->begin() + static_cast<std::ptrdiff_t>(where.slot) + 1);
  return true;
}

bool TilingModel::stackOnto(WindowId hwnd, WindowId target) {
  const std::optional<WorkspaceKey> key = keyOf(hwnd);
  if (!key || key != keyOf(target) || hwnd == target) return false;
  const Where from = find(hwnd);
  const Where to = find(target);
  if (!from.list || !to.list || from.slot == to.slot) return false;
  // Leave the old slot first, remembering the target by its window (the indexes shift).
  remove(hwnd);
  owner_[hwnd] = *key;
  const Where again = find(target);
  Slot& slot = (*again.list)[again.slot];
  slot.windows.push_back(hwnd);
  setTop(*key, slot, slot.windows.size() - 1);
  return true;
}

bool TilingModel::unstack(WindowId hwnd) {
  const std::optional<WorkspaceKey> key = keyOf(hwnd);
  const Where where = find(hwnd);
  if (!key || !where.list || (*where.list)[where.slot].windows.size() < 2) return false;
  remove(hwnd);
  owner_[hwnd] = *key;
  where.list->insert(where.list->begin() + static_cast<std::ptrdiff_t>(where.slot) + 1,
                     Slot{{hwnd}, 0});
  return true;
}

WindowId TilingModel::cycle(WindowId hwnd, int step) {
  const Where where = find(hwnd);
  if (!where.list) return 0;
  Slot& slot = (*where.list)[where.slot];
  const size_t count = slot.windows.size();
  if (count < 2) return 0;
  const size_t next = (slot.top + (step > 0 ? 1 : count - 1)) % count;
  setTop(owner_[hwnd], slot, next);
  return slot.topWindow();
}

bool TilingModel::raiseInStack(WindowId hwnd) {
  const Where where = find(hwnd);
  if (!where.list) return false;
  Slot& slot = (*where.list)[where.slot];
  if (slot.top == where.index) return false;
  setTop(owner_[hwnd], slot, where.index);
  return true;
}

void TilingModel::clear() {
  views_.clear();
  stacks_.clear();
  owner_.clear();
  layouts_.clear();
  floating_.clear();
}

}  // namespace waku::tiling

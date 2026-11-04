#include "features/tiling/tiling_model.h"

#include <algorithm>

namespace waku::tiling {

void TilingModel::add(const WorkspaceKey& key, WindowId hwnd) {
  remove(hwnd);
  floating_.erase(hwnd);
  stacks_[key].push_back(hwnd);
  owner_[hwnd] = key;
}

std::optional<WorkspaceKey> TilingModel::remove(WindowId hwnd) {
  const auto found = owner_.find(hwnd);
  if (found == owner_.end()) return std::nullopt;
  const WorkspaceKey key = found->second;
  owner_.erase(found);
  const auto stack = stacks_.find(key);
  if (stack != stacks_.end()) {
    std::erase(stack->second, hwnd);
    if (stack->second.empty()) stacks_.erase(stack);
  }
  return key;
}

std::optional<WorkspaceKey> TilingModel::keyOf(WindowId hwnd) const {
  const auto found = owner_.find(hwnd);
  if (found == owner_.end()) return std::nullopt;
  return found->second;
}

const std::vector<WindowId>& TilingModel::stack(const WorkspaceKey& key) const {
  static const std::vector<WindowId> none;
  const auto found = stacks_.find(key);
  return found == stacks_.end() ? none : found->second;
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

void TilingModel::clear() {
  stacks_.clear();
  owner_.clear();
  layouts_.clear();
  floating_.clear();
}

}  // namespace waku::tiling

#include "features/taskbar/progress_cache.h"

#include <cmath>

namespace waku::taskbar {

void ProgressCache::setState(WindowHandle hwnd, std::uint32_t pid, int state) {
  switch (static_cast<ProgressState>(state)) {
    case ProgressState::None:
    case ProgressState::Indeterminate:
    case ProgressState::Normal:
    case ProgressState::Error:
    case ProgressState::Paused:
      break;
    default:
      return;
  }
  apply(hwnd, pid, [state](WindowProgress& progress) {
    progress.state = static_cast<ProgressState>(state);
    if (progress.state == ProgressState::None) progress.value = 0;
  });
}

void ProgressCache::setValue(WindowHandle hwnd, std::uint32_t pid, double value) {
  if (!std::isfinite(value) || value < 0 || value > 1) return;
  apply(hwnd, pid, [value](WindowProgress& progress) {
    progress.value = value;
    if (progress.state != ProgressState::Error && progress.state != ProgressState::Paused)
      progress.state = ProgressState::Normal;
  });
}

void ProgressCache::apply(WindowHandle hwnd, std::uint32_t pid,
                          const std::function<void(WindowProgress&)>& update) {
  if (hwnd == 0) return;
  {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(hwnd);
    WindowProgress before;
    if (found != entries_.end() && found->second.pid == pid) before = found->second.progress;
    WindowProgress after = before;
    update(after);
    const bool sameOwner = found != entries_.end() && found->second.pid == pid;
    if (after == before && (sameOwner || after.state == ProgressState::None)) return;
    if (after.state == ProgressState::None) {
      if (found != entries_.end()) entries_.erase(found);
    } else {
      entries_[hwnd] = Entry{pid, after};
    }
  }
  emit changed(hwnd);
}

std::optional<WindowProgress> ProgressCache::get(WindowHandle hwnd, std::uint32_t pid) const {
  std::lock_guard lock(mutex_);
  const auto found = entries_.find(hwnd);
  if (found == entries_.end() || found->second.pid != pid) return std::nullopt;
  return found->second.progress;
}

void ProgressCache::forget(WindowHandle hwnd) {
  {
    std::lock_guard lock(mutex_);
    if (entries_.erase(hwnd) == 0) return;
  }
  emit changed(hwnd);
}

void ProgressCache::prune(const std::function<bool(WindowHandle, std::uint32_t)>& alive) {
  std::map<WindowHandle, Entry> entries;
  {
    std::lock_guard lock(mutex_);
    entries = entries_;
  }
  // `alive` may call into Windows: never under the lock.
  for (const auto& [hwnd, entry] : entries) {
    if (alive(hwnd, entry.pid)) continue;
    bool erased = false;
    {
      std::lock_guard lock(mutex_);
      const auto found = entries_.find(hwnd);
      if (found != entries_.end() && found->second.pid == entry.pid) {
        entries_.erase(found);
        erased = true;
      }
    }
    if (erased) emit changed(hwnd);
  }
}

std::size_t ProgressCache::size() const {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

}  // namespace waku::taskbar

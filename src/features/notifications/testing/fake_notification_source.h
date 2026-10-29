#pragma once

#include <cstdint>
#include <vector>

#include "features/notifications/notifications.h"

namespace wabar::notifications {

// Test double of NotificationSource: records commands and exposes the sink the service gave it.
class FakeSource final : public NotificationSource {
 public:
  void start(NotificationSink* sink) override { sink_ = sink; }
  void stop() override { sink_ = nullptr; }
  void clearAll() override { ++clears; }
  void refresh() override { ++refreshes; }
  void remove(std::uint32_t id) override { removed.push_back(id); }
  void activate(const NotificationItem& item) override { activated.push_back(item.id); }

  NotificationSink* sink_ = nullptr;
  int clears = 0;
  int refreshes = 0;
  std::vector<std::uint32_t> removed;
  std::vector<std::uint32_t> activated;
};

}  // namespace wabar::notifications

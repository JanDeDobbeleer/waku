#pragma once

#include <QString>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "features/brightness/brightness.h"

namespace wabar::brightness {

// Test double of BrightnessControl: two monitors, writes and refreshes recorded, no threads.
class FakeBrightness final : public BrightnessControl {
 public:
  std::vector<BrightnessMonitor> monitors() override { return list; }
  QString monitorSubtitle(qulonglong id, int index) override {
    return QStringLiteral("Monitor %1 #%2").arg(index + 1).arg(id);
  }
  std::optional<int> brightness(qulonglong id) override {
    const auto it = values.find(id);
    return it == values.end() ? std::nullopt : std::optional<int>(it->second);
  }
  void setBrightness(qulonglong id, int percent) override {
    values[id] = percent;
    writes.emplace_back(id, percent);
  }
  void refreshNow() override { ++refreshes; }
  void stop() override {
    ++stops;
    if (stopCounter) ++*stopCounter;
  }

  std::vector<BrightnessMonitor> list{{7, QStringLiteral("a")}, {9, QStringLiteral("b")}};
  std::map<qulonglong, int> values{{7, 40}, {9, 80}};
  std::vector<std::pair<qulonglong, int>> writes;
  int refreshes = 0;
  int stops = 0;
  int* stopCounter = nullptr;
};

}  // namespace wabar::brightness

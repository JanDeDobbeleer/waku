#pragma once

#include <QString>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "features/brightness/brightness.h"

namespace waku::brightness {

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
  bool supportsContrast(qulonglong id) override { return contrastValues.count(id) > 0; }
  std::optional<int> contrast(qulonglong id) override {
    const auto it = contrastValues.find(id);
    return it == contrastValues.end() ? std::nullopt : std::optional<int>(it->second);
  }
  void setContrast(qulonglong id, int percent) override {
    contrastValues[id] = percent;
    contrastWrites.emplace_back(id, percent);
  }
  void refreshNow() override { ++refreshes; }
  void stop() override {
    ++stops;
    if (stopCounter) ++*stopCounter;
  }

  std::vector<BrightnessMonitor> list{{7, QStringLiteral("a")}, {9, QStringLiteral("b")}};
  std::map<qulonglong, int> values{{7, 40}, {9, 80}};
  std::map<qulonglong, int> contrastValues;
  std::vector<std::pair<qulonglong, int>> writes;
  std::vector<std::pair<qulonglong, int>> contrastWrites;
  int refreshes = 0;
  int stops = 0;
  int* stopCounter = nullptr;
};

}  // namespace waku::brightness

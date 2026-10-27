#pragma once

#include <algorithm>
#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "core/widgets/label_format.h"
#include "core/widgets/stat_popup.h"

namespace yasb::widgets::detail {

inline QString jsonNumber(double value, bool hideDecimal) {
  return QString::fromStdString(
      yasb::widgets::pyFormat(hideDecimal ? "{v:d}" : "{v}",
                              {{"v", hideDecimal ? nlohmann::json(yasb::widgets::pyRound(value))
                                                 : nlohmann::json(value)}}));
}

template <typename Thresholds>
inline QString thresholdClass(double value, const Thresholds& thresholds, const char* prefix) {
  const char* level = value <= thresholds.low      ? "low"
                      : value <= thresholds.medium ? "medium"
                      : value <= thresholds.high   ? "high"
                                                   : "critical";
  return QString::fromLatin1(prefix) + QString::fromLatin1(level);
}

template <typename Menu>
inline StatPopupMenuConfig popupConfig(const Menu& menu) {
  StatPopupMenuConfig result;
  result.blur = menu.blur;
  result.roundCorners = menu.round_corners;
  result.roundCornersType = QString::fromStdString(menu.round_corners_type);
  result.borderColor = QString::fromStdString(menu.border_color);
  result.alignment = QString::fromStdString(menu.alignment);
  result.direction = QString::fromStdString(menu.direction);
  result.offsetLeft = menu.offset_left;
  result.offsetTop = menu.offset_top;
  result.showGraph = menu.show_graph;
  result.showGraphGrid = menu.show_graph_grid;
  result.pinIcon = QString::fromStdString(menu.pin_icon);
  result.unpinIcon = QString::fromStdString(menu.unpin_icon);
  return result;
}

inline void appendHistory(std::deque<double>& history, double value, int capacity) {
  if (capacity <= 0) return;
  while (static_cast<int>(history.size()) >= capacity) history.pop_front();
  history.push_back(value);
}

inline std::vector<double> asVector(const std::deque<double>& history) {
  return {history.begin(), history.end()};
}

template <typename Options>
inline QString className(const Options& options, const char* base) {
  return QString::fromLatin1(base) + QString::fromStdString(options.class_name);
}

inline QString optionalText(const std::optional<double>& value, int precision,
                            bool absolute = false) {
  if (!value) return QStringLiteral("N/A");
  const double v = absolute ? std::abs(*value) : *value;
  return QString::fromStdString(yasb::widgets::formatFixed(v, precision));
}

template <typename T>
inline QString optionalText(const std::optional<T>& value) {
  if (!value) return QStringLiteral("N/A");
  return QString::number(*value);
}

}  // namespace yasb::widgets::detail

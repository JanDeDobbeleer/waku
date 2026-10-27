#pragma once

#include <algorithm>
#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/widgets/label_format.h"
#include "core/widgets/stat_popup.h"

namespace yasb::widgets::detail {

inline QString stringOption(const nlohmann::json& options, const char* key,
                            const char* fallback = "") {
  return QString::fromStdString(options.value(key, std::string(fallback)));
}

inline nlohmann::json objectOption(const nlohmann::json& options, const char* key) {
  const auto it = options.find(key);
  return it != options.end() && it->is_object() ? *it : nlohmann::json::object();
}

inline QString jsonNumber(double value, bool hideDecimal) {
  return QString::fromStdString(
      yasb::widgets::pyFormat(hideDecimal ? "{v:d}" : "{v}",
                              {{"v", hideDecimal ? nlohmann::json(yasb::widgets::pyRound(value))
                                                 : nlohmann::json(value)}}));
}

inline QString thresholdClass(double value, const nlohmann::json& thresholds, const char* prefix) {
  const double low = thresholds.value("low", 25.0);
  const double medium = thresholds.value("medium", 50.0);
  const double high = thresholds.value("high", 90.0);
  const char* level = value <= low      ? "low"
                      : value <= medium ? "medium"
                      : value <= high   ? "high"
                                        : "critical";
  return QString::fromLatin1(prefix) + QString::fromLatin1(level);
}

inline StatPopupMenuConfig popupConfig(const nlohmann::json& options, bool defaultGrid) {
  const auto menu = objectOption(options, "menu");
  StatPopupMenuConfig result;
  result.blur = menu.value("blur", true);
  result.roundCorners = menu.value("round_corners", true);
  result.roundCornersType =
      QString::fromStdString(menu.value("round_corners_type", std::string("normal")));
  result.borderColor = QString::fromStdString(menu.value("border_color", std::string("System")));
  result.alignment = QString::fromStdString(menu.value("alignment", std::string("right")));
  result.direction = QString::fromStdString(menu.value("direction", std::string("down")));
  result.offsetLeft = menu.value("offset_left", 0);
  result.offsetTop = menu.value("offset_top", 6);
  result.showGraph = menu.value("show_graph", true);
  result.showGraphGrid = menu.value("show_graph_grid", defaultGrid);
  result.pinIcon = QString::fromStdString(menu.value("pin_icon", std::string()));
  result.unpinIcon = QString::fromStdString(menu.value("unpin_icon", std::string()));
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

inline QString className(const nlohmann::json& options, const char* base) {
  return QString::fromLatin1(base) +
         QString::fromStdString(options.value("class_name", std::string()));
}

inline QString callbackOption(const nlohmann::json& options, const char* button) {
  const auto callbacks = objectOption(options, "callbacks");
  return QString::fromStdString(callbacks.value(button, std::string("do_nothing")));
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

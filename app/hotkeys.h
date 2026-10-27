#pragma once

#include <windows.h>

#include <QPoint>
#include <QRect>

#include <atomic>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace yasb::app {

struct HotkeyBinding {
  std::string keys;
  std::string widget;
  std::string action;
  std::string screen = "active";
  unsigned modifiers = 0;
  unsigned virtualKey = 0;
};

struct ScreenTarget {
  std::string name;
  QRect geometry;
};

std::optional<HotkeyBinding> parseHotkey(std::string keys, std::string widget, std::string action,
                                         std::string screen = "active");
std::string routeHotkeyScreen(const std::string& mode, const QPoint& cursor, const std::vector<ScreenTarget>& screens,
                              const std::string& primaryScreen);
std::vector<HotkeyBinding> collectHotkeys(const nlohmann::json& config);

class HotkeyService {
 public:
  explicit HotkeyService(std::vector<HotkeyBinding> bindings);
  ~HotkeyService();
  HotkeyService(const HotkeyService&) = delete;
  HotkeyService& operator=(const HotkeyService&) = delete;

  void start();
  void stop();

 private:
  void run();
  std::vector<HotkeyBinding> bindings_;
  std::atomic<DWORD> threadId_{0};
  std::atomic<bool> ready_{false};
  std::thread thread_;
};

}  // namespace yasb::app

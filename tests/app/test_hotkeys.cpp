#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "app/hotkeys.h"

TEST(Hotkeys, ParsesModifiersAndRejectsMalformedBindings) {
  const auto binding = yasb::app::parseHotkey("alt+e", "wallpapers", "toggle", "cursor");
  ASSERT_TRUE(binding.has_value());
  EXPECT_EQ(binding->virtualKey, 'E');
  EXPECT_EQ(binding->modifiers, 0x0001u);
  EXPECT_EQ(binding->screen, "cursor");

  EXPECT_FALSE(yasb::app::parseHotkey("ctrl+alt", "widget", "toggle").has_value());
  EXPECT_FALSE(yasb::app::parseHotkey("ctrl+e+f", "widget", "toggle").has_value());
  EXPECT_FALSE(yasb::app::parseHotkey("ctrl+bogus", "widget", "toggle").has_value());
}

TEST(Hotkeys, CursorModeRoutesToContainingScreen) {
  const std::vector<yasb::app::ScreenTarget> screens = {{"LEFT", QRect(-1920, 0, 1920, 1080)},
                                                        {"RIGHT", QRect(0, 0, 2560, 1440)}};
  EXPECT_EQ(yasb::app::routeHotkeyScreen("cursor", QPoint(100, 100), screens, "LEFT"), "RIGHT");
  EXPECT_EQ(yasb::app::routeHotkeyScreen("cursor", QPoint(-100, 100), screens, "LEFT"), "LEFT");
  EXPECT_EQ(yasb::app::routeHotkeyScreen("primary", QPoint(100, 100), screens, "LEFT"), "LEFT");
}

TEST(Hotkeys, CollectsWidgetKeybindingsFromLoadedConfig) {
  const auto config = nlohmann::json::parse(
      R"({"widgets":{"wallpapers":{"options":{"keybindings":[{"keys":"alt+e","action":"toggle","screen":"cursor"}]}}}})");
  const auto bindings = yasb::app::collectHotkeys(config);
  ASSERT_EQ(bindings.size(), 1u);
  EXPECT_EQ(bindings.front().widget, "wallpapers");
  EXPECT_EQ(bindings.front().screen, "cursor");
}

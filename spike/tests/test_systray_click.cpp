#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "platform/systray_click.h"

using namespace yasb::platform;

namespace {

// Records every call in order, so a test can check both what was sent and in which order.
class RecordingApi final : public TrayWindowApi {
 public:
  bool isWindow(std::uint64_t hwnd) override { return windows.count(hwnd) != 0; }
  void allowSetForeground(std::uint64_t hwnd) override {
    calls.push_back("foreground:" + std::to_string(hwnd));
  }
  void sendNotifyMessage(std::uint64_t hwnd, std::uint32_t message, std::uint64_t wParam,
                         std::int64_t lParam) override {
    calls.push_back("notify:" + std::to_string(hwnd) + ":" + std::to_string(message) + ":" +
                    std::to_string(wParam) + ":" + std::to_string(lParam));
  }
  std::pair<int, int> cursorPos() override { return cursor; }
  void broadcastTaskbarCreated() override {}

  std::set<std::uint64_t> windows{100};
  std::pair<int, int> cursor{300, 40};
  std::vector<std::string> calls;
};

TrayIconData icon(std::uint32_t version, std::uint32_t uid = 7, std::uint32_t callback = 0x8001) {
  TrayIconData data;
  data.hWnd = 100;
  data.uID = uid;
  data.uCallbackMessage = callback;
  data.uVersion = version;
  return data;
}

// The recorded entry of a notification to window 100 on callback message 0x8001 (32769).
std::string expected(std::uint64_t wParam, std::int64_t lParam) {
  return "notify:100:32769:" + std::to_string(wParam) + ":" + std::to_string(lParam);
}

}  // namespace

TEST(TrayClickMessages, TheSevenMouseMessagesAreTheOnesTheOwnerMayTakeTheForegroundFor) {
  for (std::uint32_t message : {kWmLButtonDown, kWmLButtonUp, kWmLButtonDblClk, kWmRButtonDown,
                                kWmRButtonUp, kWmMButtonDown, kWmMButtonUp}) {
    EXPECT_TRUE(isTrayMouseMessage(message)) << message;
  }
  for (std::uint32_t message :
       {0x0200u /*WM_MOUSEMOVE*/, 0x0206u /*WM_RBUTTONDBLCLK*/, kNinSelect, kNinContextMenu, 0u}) {
    EXPECT_FALSE(isTrayMouseMessage(message)) << message;
  }
}

TEST(TrayClickMessages, TheConstantsAreTheWinuserValues) {
  EXPECT_EQ(kWmLButtonDown, 0x0201u);
  EXPECT_EQ(kWmLButtonUp, 0x0202u);
  EXPECT_EQ(kWmLButtonDblClk, 0x0203u);
  EXPECT_EQ(kWmRButtonDown, 0x0204u);
  EXPECT_EQ(kWmRButtonUp, 0x0205u);
  EXPECT_EQ(kWmMButtonDown, 0x0207u);
  EXPECT_EQ(kWmMButtonUp, 0x0208u);
}

TEST(TrayClickNotify, OldIconsGetTheIdInWParamAndTheEventInLParam) {
  RecordingApi api;
  sendTrayNotify(icon(0), api, kWmLButtonUp);
  ASSERT_EQ(api.calls.size(), 1u);
  EXPECT_EQ(api.calls[0], expected(7, 0x0202));
}

TEST(TrayClickNotify, Version3StillUsesTheOldLayout) {
  RecordingApi api;
  sendTrayNotify(icon(3), api, kWmLButtonUp);
  EXPECT_EQ(api.calls, (std::vector<std::string>{expected(7, 0x0202)}));
}

TEST(TrayClickNotify, Version4PacksTheCursorAndTheEventWithTheId) {
  RecordingApi api;
  api.cursor = {100, 200};
  sendTrayNotify(icon(4), api, kWmLButtonDown);
  // wParam: x in the low word, y in the high word; lParam: the event low, the icon id high.
  EXPECT_EQ(api.calls, (std::vector<std::string>{expected(0x00C80064, 0x00070201)}));
}

TEST(TrayClickNotify, ANegativeCursorCoordinateKeepsItsSixteenBits) {
  RecordingApi api;
  api.cursor = {-5, 10};  // a monitor to the left of the primary one
  sendTrayNotify(icon(4), api, kWmLButtonDown);
  EXPECT_EQ(api.calls, (std::vector<std::string>{expected(0x000AFFFB, 0x00070201)}));
}

TEST(TrayClickNotify, NothingIsSentWithoutACallbackMessage) {
  RecordingApi api;
  sendTrayNotify(icon(4, 7, 0), api, kWmLButtonDown);
  EXPECT_TRUE(api.calls.empty());
}

TEST(TrayClickAction, AMouseMessageLetsTheOwnerTakeTheForegroundFirst) {
  RecordingApi api;
  sendTrayAction(icon(0), api, kWmLButtonDown);
  EXPECT_EQ(api.calls, (std::vector<std::string>{"foreground:100", expected(7, 0x0201)}));
}

TEST(TrayClickAction, EveryMouseMessageGoesThroughTheSamePath) {
  for (std::uint32_t action : {kWmLButtonDown, kWmLButtonUp, kWmLButtonDblClk, kWmRButtonDown,
                               kWmRButtonUp, kWmMButtonDown, kWmMButtonUp}) {
    RecordingApi api;
    sendTrayAction(icon(0), api, action);
    ASSERT_GE(api.calls.size(), 2u) << action;
    EXPECT_EQ(api.calls[0], "foreground:100") << action;
    EXPECT_EQ(api.calls[1], expected(7, static_cast<std::int64_t>(action))) << action;
  }
}

TEST(TrayClickAction, OldIconsGetNoSelectOrContextMenuNotification) {
  for (std::uint32_t version : {0u, 1u, 2u}) {
    RecordingApi api;
    sendTrayAction(icon(version), api, kWmLButtonUp);
    sendTrayAction(icon(version), api, kWmRButtonUp);
    EXPECT_EQ(api.calls.size(), 4u) << version;  // two (foreground + message) pairs only
  }
}

TEST(TrayClickAction, Version3AddsSelectAfterALeftReleaseAndContextMenuAfterARightRelease) {
  RecordingApi left;
  sendTrayAction(icon(3), left, kWmLButtonUp);
  EXPECT_EQ(left.calls,
            (std::vector<std::string>{"foreground:100", expected(7, 0x0202), expected(7, 0x0400)}));

  RecordingApi right;
  sendTrayAction(icon(3), right, kWmRButtonUp);
  EXPECT_EQ(right.calls,
            (std::vector<std::string>{"foreground:100", expected(7, 0x0205), expected(7, 0x007B)}));
}

TEST(TrayClickAction, OnlyReleasesGetTheExtraNotification) {
  for (std::uint32_t action :
       {kWmLButtonDown, kWmRButtonDown, kWmMButtonDown, kWmMButtonUp, kWmLButtonDblClk}) {
    RecordingApi api;
    sendTrayAction(icon(3), api, action);
    EXPECT_EQ(api.calls.size(), 2u) << action;  // foreground + the message
  }
}

TEST(TrayClickAction, Version4KeepsTheExtraNotificationsInTheNewLayout) {
  RecordingApi api;
  api.cursor = {100, 200};
  sendTrayAction(icon(4), api, kWmLButtonUp);
  EXPECT_EQ(api.calls, (std::vector<std::string>{"foreground:100", expected(0x00C80064, 0x00070202),
                                                 expected(0x00C80064, 0x00070400)}));
}

TEST(TrayClickAction, ADeadOwnerWindowGetsNothingAtAll) {
  RecordingApi api;
  api.windows.clear();
  sendTrayAction(icon(4), api, kWmLButtonUp);
  EXPECT_TRUE(api.calls.empty());  // not even the foreground permission
}

TEST(TrayClickAction, WithoutACallbackOnlyTheForegroundPermissionIsGiven) {
  RecordingApi api;
  sendTrayAction(icon(3, 7, 0), api, kWmLButtonUp);
  EXPECT_EQ(api.calls, (std::vector<std::string>{"foreground:100"}));
}

TEST(TrayClickAction, ANonMouseActionIsSentWithoutTheForegroundPermission) {
  RecordingApi api;
  sendTrayAction(icon(0), api, kNinSelect);
  EXPECT_EQ(api.calls, (std::vector<std::string>{expected(7, 0x0400)}));
}

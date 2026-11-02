// Fakes of the tray's two OS-facing interfaces, for the Systray singleton and the Tray widget: no
// Explorer, and no real window ever receives a message.
#pragma once

#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "features/tray/tray_host.h"

namespace waku::tray {

// A host the test drives by hand. `stops` may be shared so a test can read the count after the
// host was destroyed with its owner.
class FakeHost : public TrayHost {
 public:
  explicit FakeHost(std::shared_ptr<int> stops = std::make_shared<int>(0))
      : stops_(std::move(stops)) {}
  void start() override { ++starts; }
  void stop() override { ++*stops_; }
  QString mode() const override { return QStringLiteral("fake"); }
  void sendModified(const TrayIconData& data) { emit iconModified(data); }
  void sendDeleted(const TrayIconData& data) { emit iconDeleted(data); }
  void askForIcons() { emit updateIcons(); }
  int starts = 0;

 private:
  std::shared_ptr<int> stops_;
};

// Records every call. `windows` is the set of live window handles; `anyWindow` makes every handle
// live. The observers let a test whose fake is owned by the code under test see the traffic.
class FakeApi : public TrayWindowApi {
 public:
  struct Message {
    std::uint64_t hwnd;
    std::uint32_t message;
    std::uint64_t wParam;
    std::int64_t lParam;
  };
  bool isWindow(std::uint64_t hwnd) override { return anyWindow || windows.count(hwnd) != 0; }
  void allowSetForeground(std::uint64_t hwnd) override { allowed.push_back(hwnd); }
  void sendNotifyMessage(std::uint64_t hwnd, std::uint32_t message, std::uint64_t wParam,
                         std::int64_t lParam) override {
    sent.push_back({hwnd, message, wParam, lParam});
    if (onSend) onSend(sent.back());
  }
  std::pair<int, int> cursorPos() override { return {300, 40}; }
  void broadcastTaskbarCreated() override {
    ++broadcasts;
    if (onBroadcast) onBroadcast();
  }

  std::set<std::uint64_t> windows{100, 200};
  bool anyWindow = false;
  std::vector<std::uint64_t> allowed;
  std::vector<Message> sent;
  int broadcasts = 0;
  std::function<void(const Message&)> onSend;
  std::function<void()> onBroadcast;
};

}  // namespace waku::tray

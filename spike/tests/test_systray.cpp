// The Systray singleton against a fake tray host and a fake window API: no Explorer, no real
// window ever receives a message.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <memory>
#include <set>
#include <vector>

#include "platform/systray_click.h"
#include "spike/services/systray.h"

using namespace tidy::services;
namespace plat = yasb::platform;

namespace {

class FakeHost : public plat::TrayHost {
 public:
  explicit FakeHost(std::shared_ptr<int> stops = std::make_shared<int>(0))
      : stops_(std::move(stops)) {}
  void start() override { ++starts; }
  void stop() override { ++*stops_; }
  QString mode() const override { return QStringLiteral("fake"); }
  void sendModified(const plat::TrayIconData& data) { emit iconModified(data); }
  void sendDeleted(const plat::TrayIconData& data) { emit iconDeleted(data); }
  void askForIcons() { emit updateIcons(); }
  int starts = 0;

 private:
  std::shared_ptr<int> stops_;
};

class FakeApi : public plat::TrayWindowApi {
 public:
  struct Message {
    std::uint64_t hwnd;
    std::uint32_t message;
    std::uint64_t wParam;
    std::int64_t lParam;
  };
  bool isWindow(std::uint64_t hwnd) override { return windows.count(hwnd) != 0; }
  void allowSetForeground(std::uint64_t hwnd) override { allowed.push_back(hwnd); }
  void sendNotifyMessage(std::uint64_t hwnd, std::uint32_t message, std::uint64_t wParam,
                         std::int64_t lParam) override {
    sent.push_back({hwnd, message, wParam, lParam});
  }
  std::pair<int, int> cursorPos() override { return {300, 40}; }
  void broadcastTaskbarCreated() override { ++broadcasts; }

  std::set<std::uint64_t> windows{100, 200};
  std::vector<std::uint64_t> allowed;
  std::vector<Message> sent;
  int broadcasts = 0;
};

constexpr std::uint32_t kCallback = 0x8001;

plat::TrayIconData icon(std::uint64_t hwnd, std::uint32_t uid, std::uint32_t version = 0) {
  plat::TrayIconData data;
  data.hWnd = hwnd;
  data.uID = uid;
  data.uFlags = plat::kNifMessage | plat::kNifTip;
  data.uCallbackMessage = kCallback;
  data.uVersion = version;
  data.szTip = QStringLiteral("tip %1").arg(uid);
  data.exe = QStringLiteral("app.exe");
  data.exePath = QStringLiteral("C:/app.exe");
  return data;
}

struct Fixture {
  FakeHost* host = new FakeHost;
  FakeApi* api = new FakeApi;
  Systray systray{std::unique_ptr<plat::TrayHost>(host), std::unique_ptr<plat::TrayWindowApi>(api),
                  QString()};

  int rows() { return systray.icons()->rowCount(); }
  // The callback events sent to the owner, in order (lParam of a version <= 3 notification).
  std::vector<std::int64_t> events() const {
    std::vector<std::int64_t> result;
    for (const auto& m : api->sent) {
      if (m.message == kCallback) result.push_back(m.lParam);
    }
    return result;
  }
};

}  // namespace

TEST(Systray, StartsTheHostAndIsAvailable) {
  Fixture f;
  EXPECT_TRUE(f.systray.available());
  EXPECT_EQ(f.host->starts, 1);
  EXPECT_EQ(f.rows(), 0);
}

TEST(Systray, AddUpdateDeleteReachTheModel) {
  Fixture f;
  QSignalSpy inserted(f.systray.icons(), &QAbstractItemModel::rowsInserted);
  QSignalSpy changed(f.systray.icons(), &QAbstractItemModel::dataChanged);
  QSignalSpy removed(f.systray.icons(), &QAbstractItemModel::rowsRemoved);

  f.host->sendModified(icon(100, 1));
  QCoreApplication::processEvents();
  EXPECT_EQ(f.rows(), 1);
  EXPECT_EQ(inserted.count(), 1);

  auto update = icon(100, 1);
  update.szTip = QStringLiteral("new tip");
  f.host->sendModified(update);
  QCoreApplication::processEvents();
  EXPECT_EQ(f.rows(), 1);
  EXPECT_GE(changed.count(), 1);
  EXPECT_EQ(f.systray.icons()->iconData(0)->szTip, QStringLiteral("new tip"));

  f.host->sendDeleted(icon(100, 1));
  QCoreApplication::processEvents();
  EXPECT_EQ(f.rows(), 0);
  EXPECT_EQ(removed.count(), 1);
}

TEST(Systray, SystemGuidsAndHiddenExesAreIgnored) {
  Fixture f;
  auto system = icon(100, 1);
  system.uFlags |= plat::kNifGuid;
  system.guid = plat::kTrayVolumeIconGuid;
  f.host->sendModified(system);
  EXPECT_EQ(f.rows(), 0);

  f.systray.setFilter(false, {QStringLiteral("APP.exe")});
  f.host->sendModified(icon(100, 2));
  EXPECT_EQ(f.rows(), 0);

  f.systray.setFilter(true, {});
  f.host->sendModified(system);
  f.host->sendModified(icon(100, 2));
  EXPECT_EQ(f.rows(), 2);
}

TEST(Systray, StaleSweepRemovesIconsWhoseWindowIsGone) {
  Fixture f;
  f.host->sendModified(icon(100, 1));
  f.host->sendModified(icon(200, 1));
  ASSERT_EQ(f.rows(), 2);
  f.api->windows.erase(200);
  f.host->askForIcons();
  QCoreApplication::processEvents();
  EXPECT_EQ(f.rows(), 1);
  EXPECT_EQ(f.systray.icons()->iconData(0)->hWnd, 100u);
}

TEST(Systray, ClickSendsDownAndUpForEachButton) {
  Fixture f;
  f.host->sendModified(icon(100, 1));
  f.systray.click(0, Qt::LeftButton);
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmLButtonDown, plat::kWmLButtonUp}));
  f.api->sent.clear();
  f.systray.click(0, Qt::RightButton);
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmRButtonDown, plat::kWmRButtonUp}));
  f.api->sent.clear();
  f.systray.click(0, Qt::MiddleButton);
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmMButtonDown, plat::kWmMButtonUp}));
  EXPECT_FALSE(f.api->allowed.empty());
}

TEST(Systray, VersionThreeClicksFollowWithSelectAndContextMenu) {
  Fixture f;
  f.host->sendModified(icon(100, 1, 3));
  f.systray.click(0, Qt::LeftButton);
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmLButtonDown, plat::kWmLButtonUp,
                                                   plat::kNinSelect}));
  f.api->sent.clear();
  f.systray.click(0, Qt::RightButton);
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmRButtonDown, plat::kWmRButtonUp,
                                                   plat::kNinContextMenu}));
}

TEST(Systray, DoubleClickSwallowsTheNextLeftRelease) {
  Fixture f;
  f.host->sendModified(icon(100, 1));
  f.systray.doubleClick(0);
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmLButtonDblClk}));
  f.api->sent.clear();
  f.systray.click(0, Qt::LeftButton);  // the release of the double click
  EXPECT_TRUE(f.api->sent.empty());
  f.systray.click(0, Qt::LeftButton);  // the next one is a real click
  EXPECT_EQ(f.events(), (std::vector<std::int64_t>{plat::kWmLButtonDown, plat::kWmLButtonUp}));
}

TEST(Systray, UnknownIdIsIgnored) {
  Fixture f;
  f.systray.click(42, Qt::LeftButton);
  f.systray.doubleClick(42);
  EXPECT_TRUE(f.api->sent.empty());
}

TEST(Systray, RefreshBroadcastsTaskbarCreated) {
  Fixture f;
  f.systray.refresh();
  EXPECT_EQ(f.api->broadcasts, 1);
}

TEST(Systray, NullHostIsUnavailableWithTheReason) {
  Systray systray(nullptr, std::make_unique<FakeApi>(), QStringLiteral("tray unavailable: test"));
  EXPECT_FALSE(systray.available());
  EXPECT_EQ(systray.unavailableReason(), QStringLiteral("tray unavailable: test"));
  EXPECT_EQ(systray.icons()->rowCount(), 0);
  systray.refresh();
  systray.click(0, Qt::LeftButton);
}

TEST(Systray, RealConstructorIsUnavailableOffscreen) {
  Systray systray;
  EXPECT_FALSE(systray.available());
  EXPECT_EQ(systray.unavailableReason(), QStringLiteral("tray unavailable: offscreen platform"));
}

TEST(Systray, DestructorStopsTheHostOnce) {
  auto stops = std::make_shared<int>(0);
  {
    Systray systray(std::make_unique<FakeHost>(stops), std::make_unique<FakeApi>(), QString());
  }
  EXPECT_EQ(*stops, 1);
}

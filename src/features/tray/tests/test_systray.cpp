// The Systray singleton against a fake tray host and a fake window API: no Explorer, no real
// window ever receives a message.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <memory>
#include <set>
#include <vector>

#include "features/tray/qml/systray.h"
#include "features/tray/testing/fake_tray_host.h"
#include "features/tray/tray_click.h"
#include "kernel/qt/live_desktop.h"

using namespace wabar::tray;
namespace plat = wabar::tray;

namespace {

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
  // The refresh that sweeps runs 200 ms after the request.
  ASSERT_TRUE(f.systray.refreshPending());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !f.systray.refreshPending(); }, 5000));
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

TEST(Systray, HookAskingForIconsBroadcastsTaskbarCreatedOnce) {
  Fixture f;
  f.host->askForIcons();
  f.host->askForIcons();
  // Both requests share the one 200 ms timer; once it fired nothing else is due.
  ASSERT_TRUE(QTest::qWaitFor([&] { return f.api->broadcasts > 0; }, 5000));
  EXPECT_FALSE(f.systray.refreshPending());
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
  Systray systray(nullptr);
  EXPECT_FALSE(systray.available());
  EXPECT_EQ(systray.unavailableReason(), QStringLiteral("tray unavailable: offscreen platform"));
}

TEST(Systray, NoTrayKeepsTheRealOneUnavailable) {
  wabar::kernel::setCapabilityDisabled(QStringLiteral("tray"), true);
  Systray systray(nullptr);
  wabar::kernel::setCapabilityDisabled(QStringLiteral("tray"), false);
  EXPECT_FALSE(systray.available());
  EXPECT_EQ(systray.unavailableReason(), QStringLiteral("tray unavailable: disabled by --no-tray"));
}

TEST(Systray, DestructorStopsTheHostOnce) {
  auto stops = std::make_shared<int>(0);
  {
    Systray systray(std::make_unique<FakeHost>(stops), std::make_unique<FakeApi>(), QString());
  }
  EXPECT_EQ(*stops, 1);
}

TEST(Systray, CreateUsesTheTestFactoryOncePerCall) {
  int calls = 0;
  FakeHost* host = nullptr;
  Systray::setFactoryForTests([&] {
    ++calls;
    host = new FakeHost;
    return std::make_unique<Systray>(std::unique_ptr<plat::TrayHost>(host),
                                     std::make_unique<FakeApi>(), QString());
  });
  std::unique_ptr<Systray> first(Systray::create(nullptr, nullptr));
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(first->available());
  EXPECT_EQ(host->starts, 1);
  std::unique_ptr<Systray> second(Systray::create(nullptr, nullptr));
  EXPECT_EQ(calls, 2);
  Systray::setFactoryForTests({});
}

TEST(Systray, EmptyFactoryRestoresTheRealPath) {
  Systray::setFactoryForTests([] {
    return std::make_unique<Systray>(std::make_unique<FakeHost>(), std::make_unique<FakeApi>(),
                                     QString());
  });
  Systray::setFactoryForTests({});
  std::unique_ptr<Systray> systray(Systray::create(nullptr, nullptr));
  EXPECT_FALSE(systray->available());  // offscreen
  EXPECT_EQ(systray->unavailableReason(), QStringLiteral("tray unavailable: offscreen platform"));
}

// --- icons that would show blank
// ------------------------------------------------------------------

namespace {

QStringList& trayWarnings() {
  static QStringList lines;
  return lines;
}
QtMessageHandler g_previousTrayHandler = nullptr;

void collectTrayWarnings(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  if (text.startsWith(QStringLiteral("wabar: tray icon "))) {
    trayWarnings().append(text);
    return;
  }
  if (g_previousTrayHandler) g_previousTrayHandler(type, context, text);
}

// Collects the `wabar: tray icon` lines while it lives.
struct TrayWarningCapture {
  TrayWarningCapture() {
    trayWarnings().clear();
    g_previousTrayHandler = qInstallMessageHandler(collectTrayWarnings);
  }
  ~TrayWarningCapture() { qInstallMessageHandler(g_previousTrayHandler); }
};

plat::TrayIconData withIcon(plat::TrayIconData data, char fill = '\x40') {
  data.uFlags |= plat::kNifIcon;
  data.hIcon = 0x4242;
  data.iconWidth = 16;
  data.iconHeight = 16;
  data.iconRgba = QByteArray(16 * 16 * 4, fill);
  return data;
}

QString iconUrlOf(Systray& systray, int row) {
  return systray.icons()->data(systray.icons()->index(row), TrayIconModel::IconUrlRole).toString();
}

}  // namespace

TEST(TrayIconBlank, IconThatArrivesLaterGetsAUrl) {
  TrayWarningCapture capture;
  Fixture f;
  f.host->sendModified(icon(100, 1));  // NIM_ADD without NIF_ICON
  ASSERT_EQ(f.rows(), 1);
  EXPECT_TRUE(iconUrlOf(f.systray, 0).isEmpty());
  QSignalSpy changed(f.systray.icons(), &QAbstractItemModel::dataChanged);
  f.host->sendModified(withIcon(icon(100, 1)));
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(iconUrlOf(f.systray, 0), QStringLiteral("image://tray/0/1"));
  EXPECT_FALSE(f.systray.icons()->image(0).isNull());
  EXPECT_EQ(trayWarnings(),
            QStringList{QStringLiteral("wabar: tray icon app.exe#0: no icon received yet")});
}

TEST(TrayIconBlank, TooltipAndSetVersionUpdatesKeepTheIcon) {
  TrayWarningCapture capture;
  Fixture f;
  f.host->sendModified(withIcon(icon(100, 1)));
  const QString url = iconUrlOf(f.systray, 0);
  ASSERT_FALSE(url.isEmpty());

  auto tip = icon(100, 1);  // NIM_MODIFY with NIF_TIP only, as Teams and WhatsApp send
  tip.uFlags = plat::kNifTip;
  tip.szTip = QStringLiteral("3 unread");
  f.host->sendModified(tip);
  auto setVersion = icon(100, 1);
  setVersion.messageType = plat::kNimSetVersion;
  setVersion.uFlags = 0;
  setVersion.uVersion = 4;
  f.host->sendModified(setVersion);

  EXPECT_EQ(iconUrlOf(f.systray, 0), url);
  EXPECT_FALSE(f.systray.icons()->image(0).isNull());
  EXPECT_TRUE(trayWarnings().isEmpty());
}

TEST(TrayIconBlank, UnreadableIconKeepsThePreviousImageAndLogsOnce) {
  TrayWarningCapture capture;
  Fixture f;
  f.host->sendModified(withIcon(icon(100, 1)));
  const QString url = iconUrlOf(f.systray, 0);
  auto broken = icon(100, 1);
  broken.uFlags = plat::kNifIcon;
  broken.hIcon = 0x9999;
  broken.iconError = QStringLiteral("GetIconInfo failed");
  f.host->sendModified(broken);
  f.host->sendModified(broken);
  EXPECT_EQ(iconUrlOf(f.systray, 0), url);
  EXPECT_EQ(trayWarnings(),
            QStringList{QStringLiteral(
                "wabar: tray icon app.exe#0: GetIconInfo failed, keeping the previous image")});
}

TEST(TrayIconBlank, FullyTransparentIconIsLoggedOncePerIcon) {
  TrayWarningCapture capture;
  Fixture f;
  f.host->sendModified(withIcon(icon(100, 1), '\0'));
  f.host->sendModified(withIcon(icon(100, 1), '\0'));
  f.host->sendModified(withIcon(icon(200, 1), '\0'));
  EXPECT_EQ(
      trayWarnings(),
      (QStringList{
          QStringLiteral("wabar: tray icon app.exe#0: all 16x16 pixels are fully transparent"),
          QStringLiteral("wabar: tray icon app.exe#1: all 16x16 pixels are fully transparent"),
      }));
  // A new icon after the old one was removed is logged again (ids are never reused).
  f.host->sendDeleted(icon(100, 1));
  f.host->sendModified(withIcon(icon(100, 1), '\0'));
  EXPECT_EQ(trayWarnings().size(), 3);
}
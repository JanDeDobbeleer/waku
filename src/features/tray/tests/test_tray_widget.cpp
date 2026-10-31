// The Tray widget in a real offscreen bar, on a fake tray host and a fake window API injected
// through Systray::setFactoryForTests: no Explorer, no real window ever receives a message, and
// the pin state goes to a temporary folder.
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QScreen>
#include <memory>
#include <set>
#include <vector>

#include "features/tray/qml/systray.h"
#include "features/tray/testing/fake_tray_host.h"
#include "features/tray/tray_click.h"
#include "features/tray/tray_host.h"
#include "features/tray/tray_state.h"
#include "features/tray/tray_types.h"
#include "host/testing/shell_fixtures.h"

namespace {

namespace plat = wabar::tray;
using wabar::tray::FakeApi;
using wabar::tray::FakeHost;
using wabar::tray::Systray;

constexpr std::uint32_t kCallback = 0x8001;

struct Log {
  std::vector<std::int64_t> events;  // callback lParams of version <= 3 notifications, in order
  int broadcasts = 0;
};

// A FakeApi that reports into `log`, which the test keeps after the bar owns the api.
std::unique_ptr<FakeApi> makeApi(const std::shared_ptr<Log>& log) {
  auto api = std::make_unique<FakeApi>();
  api->anyWindow = true;
  api->onSend = [log](const FakeApi::Message& m) {
    if (m.message == kCallback) log->events.push_back(m.lParam);
  };
  api->onBroadcast = [log] { ++log->broadcasts; };
  return api;
}

plat::TrayIconData makeIcon(const char* name, bool hidden = false) {
  static std::uint32_t nextId = 1;
  plat::TrayIconData d;
  d.hWnd = 100;
  d.uID = nextId++;
  d.uFlags = plat::kNifMessage | plat::kNifIcon | plat::kNifTip | (hidden ? plat::kNifState : 0);
  d.hIcon = 1;
  d.dwState = hidden ? 1 : 0;
  d.uCallbackMessage = kCallback;
  d.szTip = QString::fromLatin1(name);
  d.exe = QString::fromLatin1(name) + QStringLiteral(".exe");
  d.exePath = QStringLiteral("C:\\apps\\") + QString::fromLatin1(name) + QStringLiteral(".exe");
  d.iconWidth = 16;
  d.iconHeight = 16;
  d.iconRgba = QByteArray(16 * 16 * 4, '\x7f');
  return d;
}

class TrayWidgetTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(state_.isValid());
    Systray::setFactoryForTests([this]() -> std::unique_ptr<Systray> {
      if (!unavailable_.isEmpty()) {
        return std::make_unique<Systray>(nullptr, makeApi(log_), unavailable_);
      }
      auto fake = std::make_unique<FakeHost>();
      host_ = fake.get();
      return std::make_unique<Systray>(std::move(fake), makeApi(log_), QString());
    });
  }
  void TearDown() override { Systray::setFactoryForTests({}); }

  static QString entry(const char* name, bool pinned, int index) {
    return QStringLiteral("\"C:\\\\apps\\\\%1.exe\": {\"is_pinned\": %2, \"index\": %3}")
        .arg(QString::fromLatin1(name), pinned ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(index);
  }
  // The saved-state id of the primary screen, which the offscreen bar is on.
  static QString screenId() {
    QScreen* screen = QGuiApplication::primaryScreen();
    const QString id =
        wabar::tray::cleanScreenId(screen->manufacturer(), screen->name(), screen->serialNumber());
    return id.isEmpty() ? QStringLiteral("None") : id;
  }
  // Writes the saved state before the bar exists.
  void saveState(const QStringList& entries) {
    write(wabar::tray::SystrayStateStore(state_.path(), screenId()).path(),
          QStringLiteral("{") + entries.join(QLatin1Char(',')) + QStringLiteral("}"));
  }
  wabar::tray::IconStateMap loadState() const {
    return wabar::tray::SystrayStateStore(state_.path(), screenId()).load();
  }

  QString trayConfig(const QString& props = QString()) const {
    return QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [ Tray { objectName: "tray"; stateDir: "%1"; %2 } ]
        }
    }
}
)")
        .arg(state_.path(), props);
  }

  bool start(Harness& h, const QString& props = QString()) {
    h.start(trayConfig(props));
    if (!h.host->reload()) {
      qWarning() << "bar error:" << h.host->error();
      return false;
    }
    window_ = h.window();
    return window_ && QTest::qWaitForWindowExposed(window_);
  }
  // Tray.qml counts the shown icons in a Qt.callLater (the widget stays invisible, and takes no
  // click, until then) and the panel places them in its next polish. A fixed wait raced both: on a
  // loaded machine the click landed before the layout and the menu never opened. Run them now.
  void settleLayout() {
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    ASSERT_NE(window_, nullptr);
    window_->grabWindow();  // polishes every item before it renders the frame
  }
  void add(std::initializer_list<plat::TrayIconData> icons) {
    ASSERT_NE(host_, nullptr);
    for (const auto& icon : icons) host_->sendModified(icon);
    settleLayout();
  }

  QTemporaryDir state_;
  std::shared_ptr<Log> log_ = std::make_shared<Log>();
  FakeHost* host_ = nullptr;
  QQuickWindow* window_ = nullptr;
  QString unavailable_;
};

QString keyOf(const char* name) {
  return QStringLiteral("C:\\apps\\") + QString::fromLatin1(name) + QStringLiteral(".exe");
}

QQuickWindow* visibleWindow(const QString& name) {
  for (QWindow* window : QGuiApplication::allWindows()) {
    if (window->objectName() == name && window->isVisible()) {
      return qobject_cast<QQuickWindow*>(window);
    }
  }
  return nullptr;
}

QPoint centerIn(QQuickWindow* window, const QString& name) {
  QQuickItem* item = findItem(window, name);
  return item ? item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint()
              : QPoint(-1, -1);
}

}  // namespace

TEST_F(TrayWidgetTest, IconsAppearPinnedFirstThenUnpinnedByTheSavedState) {
  saveState({entry("a", true, 0), entry("b", false, 0), entry("c", true, 1)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a"), makeIcon("b"), makeIcon("c")});

  QQuickItem* tray = findItem(h.window(), QStringLiteral("tray"));
  ASSERT_NE(tray, nullptr);
  EXPECT_TRUE(tray->isVisible());
  // Registry ids follow arrival: a=0, b=1, c=2.
  for (const char* name : {"trayIcon-0", "trayIcon-1", "trayIcon-2"}) {
    QQuickItem* icon = findItem(h.window(), QString::fromLatin1(name));
    ASSERT_NE(icon, nullptr) << name;
    EXPECT_TRUE(icon->isVisible()) << name;
    EXPECT_EQ(icon->width(), 24) << name;
  }
  const qreal a = positionIn(h.window(), QStringLiteral("trayIcon-0")).x();
  const qreal b = positionIn(h.window(), QStringLiteral("trayIcon-1")).x();
  const qreal c = positionIn(h.window(), QStringLiteral("trayIcon-2")).x();
  EXPECT_LT(a, c);
  EXPECT_LT(c, b);
  EXPECT_FALSE(findItem(h.window(), QStringLiteral("trayUnavailable"))->isVisible());
}

TEST_F(TrayWidgetTest, TheChevronOpensAPopupGridWithIconsPerRowColumns) {
  Harness h;
  ASSERT_TRUE(start(h, QStringLiteral("unpinnedInPopup: true; iconsPerRow: 2")));
  QQuickItem* chevron = findItem(h.window(), QStringLiteral("trayChevron"));
  ASSERT_NE(chevron, nullptr);
  EXPECT_FALSE(chevron->isVisible());  // nothing to show yet
  add({makeIcon("a"), makeIcon("b"), makeIcon("c")});

  EXPECT_TRUE(chevron->isVisible());
  EXPECT_EQ(chevron->property("text").toString(), QString(QChar(0x25BE)));
  EXPECT_EQ(findItem(h.window(), QStringLiteral("trayIcon-0")), nullptr);  // not in the bar

  QQuickWindow* popup = findWindow(QStringLiteral("trayOverflow"));
  ASSERT_NE(popup, nullptr);
  EXPECT_FALSE(popup->isVisible());
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("trayChevron")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->isVisible(); }, 2000));
  ASSERT_TRUE(QTest::qWaitForWindowExposed(popup));
  EXPECT_EQ(chevron->property("text").toString(), QString(QChar(0x25B4)));

  QQuickItem* grid = findItem(popup, QStringLiteral("trayOverflowGrid"));
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->property("columns").toInt(), 2);
  const QPointF i0 = positionIn(popup, QStringLiteral("trayIcon-0"));
  const QPointF i1 = positionIn(popup, QStringLiteral("trayIcon-1"));
  const QPointF i2 = positionIn(popup, QStringLiteral("trayIcon-2"));
  EXPECT_EQ(i0.y(), i1.y());
  EXPECT_GT(i1.x(), i0.x());
  EXPECT_GT(i2.y(), i0.y());
  EXPECT_EQ(i2.x(), i0.x());

  // A click on an icon reaches the program and closes the popup.
  QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier,
                    centerIn(popup, QStringLiteral("trayIcon-1")));
  EXPECT_TRUE(QTest::qWaitFor([&] { return !popup->isVisible(); }, 2000));
  EXPECT_EQ(log_->events,
            (std::vector<std::int64_t>{static_cast<std::int64_t>(plat::kWmLButtonDown),
                                       static_cast<std::int64_t>(plat::kWmLButtonUp)}));
}

TEST_F(TrayWidgetTest, LeftClickSendsPressAndRelease) {
  saveState({entry("a", true, 0)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a")});
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-0")));
  EXPECT_EQ(log_->events,
            (std::vector<std::int64_t>{static_cast<std::int64_t>(plat::kWmLButtonDown),
                                       static_cast<std::int64_t>(plat::kWmLButtonUp)}));
}

TEST_F(TrayWidgetTest, RightClickSendsPressAndRelease) {
  saveState({entry("a", true, 0)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a")});
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-0")));
  EXPECT_EQ(log_->events,
            (std::vector<std::int64_t>{static_cast<std::int64_t>(plat::kWmRButtonDown),
                                       static_cast<std::int64_t>(plat::kWmRButtonUp)}));
  EXPECT_EQ(visibleWindow(QStringLiteral("trayMenu")), nullptr);  // a plain right click: no menu
}

TEST_F(TrayWidgetTest, MiddleClickSendsPressAndRelease) {
  saveState({entry("a", true, 0)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a")});
  QTest::mouseClick(h.window(), Qt::MiddleButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-0")));
  EXPECT_EQ(log_->events,
            (std::vector<std::int64_t>{static_cast<std::int64_t>(plat::kWmMButtonDown),
                                       static_cast<std::int64_t>(plat::kWmMButtonUp)}));
}

TEST_F(TrayWidgetTest, DoubleClickSendsTheDoubleClickMessage) {
  saveState({entry("a", true, 0)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a")});
  QTest::mouseDClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                     centerIn(h.window(), QStringLiteral("trayIcon-0")));
  ASSERT_GE(log_->events.size(), 3u);
  // Press and release of the first click, then the double click; its release is swallowed.
  EXPECT_EQ(log_->events,
            (std::vector<std::int64_t>{static_cast<std::int64_t>(plat::kWmLButtonDown),
                                       static_cast<std::int64_t>(plat::kWmLButtonUp),
                                       static_cast<std::int64_t>(plat::kWmLButtonDblClk)}));

  // The next single click is a real one again.
  log_->events.clear();
  // No wait for the double-click interval: QTest moves its event clock past it after each click.
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-0")));
  EXPECT_EQ(log_->events,
            (std::vector<std::int64_t>{static_cast<std::int64_t>(plat::kWmLButtonDown),
                                       static_cast<std::int64_t>(plat::kWmLButtonUp)}));
}

TEST_F(TrayWidgetTest, ControlClickPinsAndSendsNothing) {
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a"), makeIcon("b")});  // both unpinned, inline
  const qreal before = positionIn(h.window(), QStringLiteral("trayIcon-1")).x();

  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::ControlModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-1")));
  settleLayout();
  EXPECT_TRUE(log_->events.empty());
  // Pinned icons come first, so b moved in front of a.
  EXPECT_LT(positionIn(h.window(), QStringLiteral("trayIcon-1")).x(), before);
  const auto state = loadState();
  const auto* saved = state.find(keyOf("b"));
  ASSERT_NE(saved, nullptr);
  EXPECT_TRUE(saved->isPinned);
}

TEST_F(TrayWidgetTest, TheMenuMovesAnIconLeftAndSavesTheOrder) {
  saveState({entry("a", true, 0), entry("c", true, 1)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a"), makeIcon("c")});  // ids 0 and 1
  EXPECT_LT(positionIn(h.window(), QStringLiteral("trayIcon-0")).x(),
            positionIn(h.window(), QStringLiteral("trayIcon-1")).x());

  QTest::mouseClick(h.window(), Qt::RightButton, Qt::ControlModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-1")));
  // The tap opens the menu inside the click.
  QQuickWindow* menu = visibleWindow(QStringLiteral("trayMenu"));
  ASSERT_NE(menu, nullptr);
  ASSERT_TRUE(QTest::qWaitForWindowExposed(menu));
  EXPECT_TRUE(log_->events.empty());  // the menu click does not reach the program
  EXPECT_EQ(findItem(menu, QStringLiteral("trayMenuPin"))->property("text").toString(),
            QStringLiteral("Unpin"));

  menu->grabWindow();  // the menu's column is placed in its polish
  QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    centerIn(menu, QStringLiteral("trayMenuLeft")));
  settleLayout();
  EXPECT_LT(positionIn(h.window(), QStringLiteral("trayIcon-1")).x(),
            positionIn(h.window(), QStringLiteral("trayIcon-0")).x());
  EXPECT_FALSE(menu->isVisible());

  const auto state = loadState();
  const auto* a = state.find(keyOf("a"));
  const auto* c = state.find(keyOf("c"));
  ASSERT_NE(a, nullptr);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->index, 0);
  EXPECT_EQ(a->index, 1);
  EXPECT_TRUE(log_->events.empty());
}

TEST_F(TrayWidgetTest, TheMenuRefreshAsksExplorerForTheIcons) {
  saveState({entry("a", true, 0)});
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a")});
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::ControlModifier,
                    centerIn(h.window(), QStringLiteral("trayIcon-0")));
  QQuickWindow* menu = visibleWindow(QStringLiteral("trayMenu"));
  ASSERT_NE(menu, nullptr);
  ASSERT_TRUE(QTest::qWaitForWindowExposed(menu));
  menu->grabWindow();  // the menu's column is placed in its polish
  QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    centerIn(menu, QStringLiteral("trayMenuRefresh")));
  EXPECT_EQ(log_->broadcasts, 1);
}

TEST_F(TrayWidgetTest, AnIconThatAsksToBeHiddenIsNotShown) {
  Harness h;
  ASSERT_TRUE(start(h));
  add({makeIcon("a", true)});
  QQuickItem* icon = findItem(h.window(), QStringLiteral("trayIcon-0"));
  ASSERT_NE(icon, nullptr);
  EXPECT_FALSE(icon->isVisible());
  EXPECT_EQ(icon->width(), 0);
  EXPECT_EQ(icon->height(), 0);
  EXPECT_FALSE(findItem(h.window(), QStringLiteral("tray"))->isVisible());  // nothing to show
}

TEST_F(TrayWidgetTest, TheWidgetSaysWhyTheTrayIsUnavailable) {
  unavailable_ = QStringLiteral("tray unavailable: test reason");
  Harness h;
  ASSERT_TRUE(start(h));
  QQuickItem* pill = findItem(h.window(), QStringLiteral("trayUnavailable"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
  EXPECT_EQ(pill->property("text").toString(), QStringLiteral("tray unavailable"));
  EXPECT_EQ(pill->property("tooltip").toString(), QStringLiteral("tray unavailable: test reason"));
  EXPECT_TRUE(findItem(h.window(), QStringLiteral("tray"))->isVisible());
}

TEST_F(TrayWidgetTest, HideFiltersIconsByExecutableName) {
  Harness h;
  ASSERT_TRUE(start(h, QStringLiteral(R"(hide: ["x.exe"])")));
  add({makeIcon("x"), makeIcon("a")});
  // The filtered icon never gets a row; the other one is the first admitted.
  EXPECT_NE(findItem(h.window(), QStringLiteral("trayIcon-0")), nullptr);
  EXPECT_EQ(findItem(h.window(), QStringLiteral("trayIcon-1")), nullptr);
}

TEST_F(TrayWidgetTest, ShowUnpinnedFalseHidesTheUnpinnedIcons) {
  saveState({entry("a", true, 0)});
  Harness h;
  ASSERT_TRUE(start(h, QStringLiteral("showUnpinned: false")));
  add({makeIcon("a"), makeIcon("b")});
  EXPECT_NE(findItem(h.window(), QStringLiteral("trayIcon-0")), nullptr);
  EXPECT_EQ(findItem(h.window(), QStringLiteral("trayIcon-1")), nullptr);
}

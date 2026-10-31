#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <atomic>
#include <map>

#include "features/taskbar/app_identity.h"
#include "features/taskbar/pin_store.h"
#include "features/taskbar/pin_support.h"
#include "features/taskbar/taskbar_icons.h"
#include "features/taskbar/testing/fake_window_system.h"
#include "features/taskbar/window_system.h"
#include "features/taskbar/window_tracker.h"

using namespace wabar::taskbar;

namespace {

constexpr const char* kRecycleBinId = "explorer:::{645FF040-5081-101B-9F08-00AA002F954E}";

constexpr WindowHandle kFirst = 101;
constexpr WindowHandle kSecond = 102;
constexpr WindowHandle kThird = 103;

}  // namespace

// --- window tracker

class TrackerTest : public testing::Test {
 protected:
  void SetUp() override {
    system_ = std::make_shared<FakeSystem>(QStringLiteral("explorer.exe"));
    tracker_ = std::make_unique<WindowTracker>(system_);
    QObject::connect(
        tracker_.get(), &WindowTracker::windowAdded,
        [this](WindowHandle h, const WindowInfo&) { events << QStringLiteral("add %1").arg(h); });
    QObject::connect(tracker_.get(), &WindowTracker::windowRemoved,
                     [this](WindowHandle h, const WindowInfo&) {
                       events << QStringLiteral("remove %1").arg(h);
                     });
    QObject::connect(tracker_.get(), &WindowTracker::windowUpdated,
                     [this](WindowHandle h, const WindowInfo&) {
                       events << QStringLiteral("update %1").arg(h);
                     });
    QObject::connect(tracker_.get(), &WindowTracker::windowMinimizeChanged,
                     [this](WindowHandle h, bool m) {
                       events << QStringLiteral("minimize %1 %2").arg(h).arg(m);
                     });
  }

  std::shared_ptr<FakeSystem> system_;
  std::unique_ptr<WindowTracker> tracker_;
  QStringList events;
};

TEST_F(TrackerTest, StartEnumeratesTaskbarWindowsOnly) {
  system_->add(kFirst);
  system_->add(kSecond).taskbar = false;
  system_->foreground = kFirst;
  EXPECT_FALSE(tracker_->start(0));
  ASSERT_TRUE(tracker_->start(1));
  EXPECT_EQ(events, (QStringList{"add 101"}));
  EXPECT_TRUE(tracker_->isActive(kFirst));
  EXPECT_FALSE(tracker_->isTracked(kSecond));
}

TEST_F(TrackerTest, ShellHookCreateDestroyAndActivate) {
  ASSERT_TRUE(tracker_->start(1));
  system_->add(kFirst);
  system_->add(kSecond);
  tracker_->handleShellHook(shell::kWindowCreated, kFirst);
  tracker_->handleShellHook(shell::kWindowCreated, kSecond);
  EXPECT_EQ(events, (QStringList{"add 101", "add 102"}));

  events.clear();
  tracker_->handleShellHook(shell::kWindowActivated, kFirst);
  EXPECT_EQ(events, (QStringList{"update 101"}));
  events.clear();
  tracker_->handleShellHook(shell::kWindowActivated, kSecond);
  EXPECT_EQ(events.size(), 2);
  EXPECT_FALSE(tracker_->isActive(kFirst));
  EXPECT_TRUE(tracker_->isActive(kSecond));

  events.clear();
  tracker_->handleShellHook(shell::kWindowDestroyed, kFirst);
  EXPECT_EQ(events, (QStringList{"remove 101"}));
  EXPECT_FALSE(tracker_->isTracked(kFirst));
}

TEST_F(TrackerTest, ActivatingAnUnknownWindowAddsItActive) {
  ASSERT_TRUE(tracker_->start(1));
  system_->add(kFirst);
  tracker_->handleShellHook(shell::kRudeAppActivated, kFirst);
  EXPECT_TRUE(tracker_->isActive(kFirst));
  EXPECT_EQ(events, (QStringList{"add 101"}));
}

TEST_F(TrackerTest, FlashSetsTheFlagOnceAndActivationClearsIt) {
  system_->add(kFirst);
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  tracker_->handleShellHook(shell::kFlash, kFirst);
  EXPECT_TRUE(tracker_->isFlashing(kFirst));
  EXPECT_EQ(events, (QStringList{"update 101"}));
  events.clear();
  tracker_->handleShellHook(shell::kFlash, kFirst);  // unchanged: nothing to report
  EXPECT_TRUE(events.isEmpty());
  tracker_->handleShellHook(shell::kWindowActivated, kFirst);
  EXPECT_FALSE(tracker_->isFlashing(kFirst));
}

TEST_F(TrackerTest, RedrawIsDebouncedAndReportsTitleChanges) {
  system_->add(kFirst, QStringLiteral("Before"));
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  system_->rows[kFirst].info.title = QStringLiteral("After");
  tracker_->handleShellHook(shell::kRedraw, kFirst);
  tracker_->handleShellHook(shell::kRedraw, kFirst);
  EXPECT_TRUE(events.isEmpty());
  QTest::qWait(150);
  EXPECT_EQ(events, (QStringList{"update 101"}));
  EXPECT_EQ(tracker_->window(kFirst)->title, QStringLiteral("After"));
}

TEST_F(TrackerTest, CloakedWindowsAreDroppedUnlessKeptOrUwp) {
  system_->add(kFirst);
  system_->add(kSecond).info.className = QStringLiteral("ApplicationFrameWindow");
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  tracker_->handleWinEvent(shell::kEventObjectCloaked, kFirst, 0, 0);
  tracker_->handleWinEvent(shell::kEventObjectCloaked, kSecond, 0, 0);
  QTest::qWait(120);
  EXPECT_FALSE(tracker_->isTracked(kFirst));
  EXPECT_TRUE(tracker_->isTracked(kSecond));
  EXPECT_TRUE(events.contains(QStringLiteral("remove 101")));
  EXPECT_TRUE(events.contains(QStringLiteral("update 102")));

  // Uncloaking brings the dropped window back.
  events.clear();
  tracker_->handleWinEvent(shell::kEventObjectUncloaked, kFirst, 0, 0);
  QTest::qWait(150);
  EXPECT_EQ(events, (QStringList{"add 101"}));
}

TEST_F(TrackerTest, KeepCloakedTasksKeepsAndRefreshes) {
  system_->add(kFirst);
  ASSERT_TRUE(tracker_->start(1));
  tracker_->keepCloakedTasks(true);
  tracker_->keepCloakedTasks(false);  // never downgrades
  EXPECT_TRUE(tracker_->keepsCloakedTasks());
  events.clear();
  system_->rows[kFirst].info.isCloaked = true;
  tracker_->handleWinEvent(shell::kEventObjectCloaked, kFirst, 0, 0);
  QTest::qWait(120);
  EXPECT_TRUE(tracker_->isTracked(kFirst));
  EXPECT_EQ(events, (QStringList{"update 101"}));
}

TEST_F(TrackerTest, WinEventsFromChildObjectsAreIgnoredAndHideRemoves) {
  system_->add(kFirst);
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  tracker_->handleWinEvent(shell::kEventObjectHide, kFirst, 1, 0);
  tracker_->handleWinEvent(shell::kEventObjectHide, 0, 0, 0);
  QTest::qWait(20);
  EXPECT_TRUE(events.isEmpty());
  tracker_->handleWinEvent(shell::kEventObjectHide, kFirst, 0, 0);
  QTest::qWait(20);
  EXPECT_EQ(events, (QStringList{"remove 101"}));
}

TEST_F(TrackerTest, MinimizeEventsOnlyForTrackedWindows) {
  system_->add(kFirst);
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  tracker_->handleWinEvent(shell::kEventSystemMinimizeStart, kFirst, 0, 0);
  tracker_->handleWinEvent(shell::kEventSystemMinimizeEnd, kFirst, 0, 0);
  tracker_->handleWinEvent(shell::kEventSystemMinimizeStart, kSecond, 0, 0);
  QTest::qWait(20);
  EXPECT_EQ(events, (QStringList{"minimize 101 1", "minimize 101 0"}));
}

TEST_F(TrackerTest, WindowThatStopsQualifyingIsRemovedOnUpdate) {
  system_->add(kFirst);
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  system_->rows[kFirst].taskbar = false;
  tracker_->handleShellHook(shell::kWindowReplacing, kFirst);
  EXPECT_EQ(events, (QStringList{"remove 101"}));
}

// The system-wide show events fire for every tooltip, menu and child control: those never cost
// the full query (process, DWM, title).
TEST_F(TrackerTest, AWindowThatCannotQualifyIsSkippedWithoutAQuery) {
  ASSERT_TRUE(tracker_->start(1));
  for (WindowHandle hwnd = 200; hwnd < 300; ++hwnd) system_->add(hwnd).candidate = false;
  events.clear();
  system_->queries = 0;
  for (WindowHandle hwnd = 200; hwnd < 300; ++hwnd)
    tracker_->handleWinEvent(shell::kEventObjectShow, hwnd, 0, 0);
  QTest::qWait(20);
  EXPECT_TRUE(events.isEmpty());
  EXPECT_EQ(system_->queries, 0);  // before: one per event
}

TEST_F(TrackerTest, AUwpFrameThatCannotQualifyYetIsTriedAgain) {
  ASSERT_TRUE(tracker_->start(1));
  FakeSystem::Row& row = system_->add(kFirst);
  row.info.className = QStringLiteral("ApplicationFrameWindow");
  row.candidate = false;
  tracker_->handleWinEvent(shell::kEventObjectShow, kFirst, 0, 0);
  QTest::qWait(20);
  EXPECT_FALSE(tracker_->isTracked(kFirst));
  system_->rows[kFirst].candidate = true;  // it became a visible top-level frame
  EXPECT_TRUE(QTest::qWaitFor([&] { return tracker_->isTracked(kFirst); }, 2000));
}

TEST_F(TrackerTest, AnEventQueriesTheWindowOnce) {
  system_->add(kFirst, QStringLiteral("Before"));
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  system_->queries = 0;
  system_->rows[kFirst].info.title = QStringLiteral("After");
  tracker_->handleShellHook(shell::kWindowReplacing, kFirst);  // an update
  EXPECT_EQ(system_->queries, 1);                              // before: four
  EXPECT_EQ(events, (QStringList{"update 101"}));
  EXPECT_EQ(tracker_->window(kFirst)->title, QStringLiteral("After"));

  system_->queries = 0;
  system_->add(kSecond);
  tracker_->handleShellHook(shell::kWindowCreated, kSecond);  // an add
  EXPECT_EQ(system_->queries, 1);                             // before: three
  system_->queries = 0;
  tracker_->handleWinEvent(shell::kEventObjectCloaked, kSecond, 0, 0);
  QTest::qWait(120);
  EXPECT_EQ(system_->queries, 2);  // the cloak read and the removal snapshot; before: three
  EXPECT_FALSE(tracker_->isTracked(kSecond));
}

TEST_F(TrackerTest, RedrawsWithinTheDelayShareOneTimer) {
  system_->add(kFirst, QStringLiteral("Before"));
  ASSERT_TRUE(tracker_->start(1));
  events.clear();
  system_->rows[kFirst].info.title = QStringLiteral("After");
  for (int i = 0; i < 20; ++i) tracker_->handleShellHook(shell::kRedraw, kFirst);
  EXPECT_EQ(tracker_->findChildren<QTimer*>().size(), 1);
  EXPECT_TRUE(events.isEmpty());
  QTest::qWait(150);
  EXPECT_EQ(events, (QStringList{"update 101"}));
}

// --- platform helpers

TEST(TaskbarIdentity, CommandLineArgumentsSplitOffTheProgram) {
  EXPECT_EQ(commandLineArguments(QStringLiteral("\"C:\\Windows\\system32\\mmc.exe\" devmgmt.msc")),
            QStringLiteral("devmgmt.msc"));
  EXPECT_EQ(commandLineArguments(QStringLiteral("rundll32.exe shell32.dll,Control_RunDLL")),
            QStringLiteral("shell32.dll,Control_RunDLL"));
  EXPECT_EQ(commandLineArguments(QStringLiteral("\"C:\\a b\\x.exe\"")), QString());
  EXPECT_EQ(commandLineArguments(QStringLiteral("x.exe")), QString());
}

TEST(TaskbarIdentity, VersionedAppPathsHaveAStableVariant) {
  const auto electron = normalizedTargets(QStringLiteral("C:\\Apps\\Chat\\app-1.2.3\\Chat.exe"));
  ASSERT_EQ(electron.size(), 2u);
  EXPECT_EQ(electron[1], QStringLiteral("c:\\apps\\chat\\chat.exe"));

  const auto scoop =
      normalizedTargets(QStringLiteral("C:\\Users\\u\\scoop\\apps\\tool\\1.2.3.4\\tool.exe"));
  ASSERT_EQ(scoop.size(), 2u);
  EXPECT_EQ(scoop[1], QStringLiteral("c:\\users\\u\\scoop\\apps\\tool\\tool.exe"));

  EXPECT_EQ(normalizedTargets(QStringLiteral("C:\\Tools\\plain\\plain.exe")).size(), 1u);
  EXPECT_TRUE(normalizedTargets(QString()).empty());
}

TEST(TaskbarIcons, SynchronousLoadScalesToThePhysicalSize) {
  auto system = std::make_shared<FakeSystem>(QStringLiteral("explorer.exe"));
  system->add(kFirst);
  TaskbarIcons icons(system, /*async=*/false);
  const QPixmap pixmap = icons.icon(kFirst, 16, 1.5);
  ASSERT_FALSE(pixmap.isNull());
  EXPECT_EQ(pixmap.width(), 24);
  EXPECT_DOUBLE_EQ(pixmap.devicePixelRatio(), 1.5);
  EXPECT_TRUE(icons.icon(kFirst, 0, 1.0).isNull());
}

TEST(TaskbarIcons, ASilentWindowIsAskedLessAndLessOften) {
  EXPECT_EQ(TaskbarIcons::retryDelayMs(0), 2000);
  EXPECT_EQ(TaskbarIcons::retryDelayMs(1), 4000);
  EXPECT_EQ(TaskbarIcons::retryDelayMs(2), 8000);
  EXPECT_EQ(TaskbarIcons::retryDelayMs(3), 16000);
  EXPECT_EQ(TaskbarIcons::retryDelayMs(4), 32000);
  EXPECT_EQ(TaskbarIcons::retryDelayMs(40), TaskbarIcons::kMaxRetryMs);
}

TEST(TaskbarPinIcons, ToastAumidIconIsCroppedAndPaddedWhenTheShellHasNone) {
  QTemporaryDir dir;
  QImage tile(40, 40, QImage::Format_ARGB32);
  tile.fill(Qt::transparent);
  // A 20x10 drawing inside a transparent tile margin.
  for (int y = 15; y < 25; ++y)
    for (int x = 10; x < 30; ++x) tile.setPixel(x, y, qRgba(255, 0, 0, 255));
  const QString path = dir.filePath(QStringLiteral("toast.png"));
  ASSERT_TRUE(tile.save(path));

  FakePins pins;
  pins.noShellIcon = true;
  pins.toastPath = path;
  const IconBitmap bitmap =
      loadPinnedIcon(pins, QStringLiteral("aumid:Unpackaged.Toast"), PinMetadata{}, 16);
  ASSERT_EQ(bitmap.width, 16);
  ASSERT_EQ(bitmap.height, 16);
  const QImage image(reinterpret_cast<const uchar*>(bitmap.bgra.constData()), 16, 16, 64,
                     QImage::Format_ARGB32);
  // The drawing fills the width and is centred vertically: 16x8 on a 16x16 canvas.
  EXPECT_EQ(qAlpha(image.pixel(8, 8)), 255);
  EXPECT_EQ(qAlpha(image.pixel(1, 8)), 255);
  EXPECT_EQ(qAlpha(image.pixel(8, 0)), 0);
  EXPECT_EQ(qAlpha(image.pixel(8, 15)), 0);

  // The shell's own icon wins, and only AUMID apps look at the toast registration.
  pins.noShellIcon = false;
  EXPECT_EQ(
      loadPinnedIcon(pins, QStringLiteral("aumid:Unpackaged.Toast"), PinMetadata{}, 16).bgra.at(0),
      '\x30');
  pins.noShellIcon = true;
  EXPECT_EQ(loadPinnedIcon(pins, QStringLiteral("path:c:\\x.exe"), PinMetadata{}, 16).bgra.at(0),
            '\x77');
  // No toast icon either: the generic application icon.
  pins.toastPath.reset();
  EXPECT_EQ(loadPinnedIcon(pins, QStringLiteral("aumid:Nothing"), PinMetadata{}, 16).bgra.at(0),
            '\x77');
}

// --- store and id helpers

TEST(TaskbarPinStore, SavesLoadsAndSignalsEveryChange) {
  QTemporaryDir dir;
  const QString file = dir.filePath(QStringLiteral("sub/taskbar_pinned.json"));
  PinStore store(file);
  store.load();
  EXPECT_TRUE(store.isEmpty());
  QSignalSpy spy(&store, &PinStore::pinnedAppsChanged);

  store.pin(QStringLiteral("a"), QJsonObject{{"title", "A"}});
  store.pin(QStringLiteral("b"), QJsonObject{{"title", "B"}, {"shortcut_path", "C:\\b.lnk"}}, 0);
  EXPECT_EQ(store.order(), (QStringList{"b", "a"}));
  EXPECT_EQ(store.shortcutPath(QStringLiteral("b")), QStringLiteral("C:\\b.lnk"));
  EXPECT_EQ(store.title(QStringLiteral("a")), QStringLiteral("A"));

  store.updateOrder({"b", "a"});  // unchanged: no signal
  store.updateOrder({"a", "b"});
  EXPECT_FALSE(store.unpin(QStringLiteral("missing")));
  EXPECT_TRUE(store.unpin(QStringLiteral("a")));

  ASSERT_EQ(spy.count(), 4);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("pin"));
  EXPECT_EQ(spy.at(2).at(0).toString(), QStringLiteral("reorder"));
  EXPECT_EQ(spy.at(2).at(1).toString(), QString());
  EXPECT_EQ(spy.at(3).at(0).toString(), QStringLiteral("unpin"));
  // Pinning an id again moves it to the requested position.
  store.pin(QStringLiteral("c"), QJsonObject{{"title", "C"}});
  store.pin(QStringLiteral("c"), QJsonObject{{"title", "C"}}, 0);
  EXPECT_EQ(store.order(), (QStringList{"c", "b"}));

  PinStore reloaded(file);
  reloaded.load();
  EXPECT_EQ(reloaded.order(), (QStringList{"c", "b"}));
  EXPECT_TRUE(reloaded.contains(QStringLiteral("b")));
  EXPECT_FALSE(reloaded.contains(QStringLiteral("a")));
}

TEST(TaskbarPinStore, AnUnreadableFileMeansNothingIsPinned) {
  QTemporaryDir dir;
  const QString file = dir.filePath(QStringLiteral("taskbar_pinned.json"));
  QFile broken(file);
  ASSERT_TRUE(broken.open(QIODevice::WriteOnly));
  broken.write("{not json");
  broken.close();
  PinStore store(file);
  store.load();
  EXPECT_TRUE(store.isEmpty());
  EXPECT_TRUE(store.order().isEmpty());
}

TEST(TaskbarPinIds, UniqueIdsSplitIntoTypeValueAndArguments) {
  const UniqueIdParts mmc = parseUniqueId(QStringLiteral("path:C:\\Windows\\mmc.exe|eventvwr.msc"));
  EXPECT_EQ(mmc.type, QStringLiteral("path"));
  EXPECT_EQ(mmc.value, QStringLiteral("C:\\Windows\\mmc.exe"));
  EXPECT_EQ(mmc.args, QStringLiteral("eventvwr.msc"));
  const UniqueIdParts shell = parseUniqueId(QStringLiteral("explorer:shell:RecycleBinFolder"));
  EXPECT_EQ(shell.value, QStringLiteral("shell:RecycleBinFolder"));
  const UniqueIdParts clsid = parseUniqueId(QString::fromLatin1(kRecycleBinId));
  EXPECT_EQ(clsid.type, QStringLiteral("explorer"));
  EXPECT_EQ(clsid.value, QStringLiteral("::{645FF040-5081-101B-9F08-00AA002F954E}"));
  EXPECT_EQ(parseUniqueId(QStringLiteral("whatever")).type, QStringLiteral("unknown"));
  EXPECT_TRUE(isRecycleBinId(QStringLiteral("explorer:::{645ff040-5081-101b-9f08-00aa002f954e}")));
  EXPECT_FALSE(isRecycleBinId(QStringLiteral("explorer:C:\\Work")));
}

TEST(TaskbarPinIds, ToolFilesComeFromHostArguments) {
  QTemporaryDir dir;
  const QString tool = dir.filePath(QStringLiteral("tool.msc"));
  QFile file(tool);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.close();
  QFile applet(dir.filePath(QStringLiteral("main.cpl")));
  ASSERT_TRUE(applet.open(QIODevice::WriteOnly));
  applet.close();
  const QString system32 = dir.path();

  EXPECT_EQ(QDir::cleanPath(toolFileFromArguments(
                QStringLiteral("\"") + tool + QStringLiteral("\" /s"), system32)),
            tool);
  // Relative names resolve against System32; a rundll32 argument lists the file after a comma.
  EXPECT_EQ(QDir::cleanPath(toolFileFromArguments(QStringLiteral("tool.msc"), system32)), tool);
  EXPECT_EQ(QDir::cleanPath(toolFileFromArguments(
                QStringLiteral("shell32.dll,Control_RunDLL main.cpl,,1"), system32)),
            dir.filePath(QStringLiteral("main.cpl")));
  EXPECT_TRUE(toolFileFromArguments(QStringLiteral("missing.msc"), system32).isEmpty());
  EXPECT_TRUE(toolFileFromArguments(QString(), system32).isEmpty());
}

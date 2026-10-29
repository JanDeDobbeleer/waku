#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <map>

#include "platform/app_identity.h"
#include "platform/pin_support.h"
#include "platform/window_system.h"
#include "platform/window_tracker.h"
#include "widgets/taskbar/pin_store.h"
#include "widgets/taskbar/taskbar_icons.h"

using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

constexpr const char* kRecycleBinId = "explorer:::{645FF040-5081-101B-9F08-00AA002F954E}";

constexpr WindowHandle kFirst = 101;
constexpr WindowHandle kSecond = 102;
constexpr WindowHandle kThird = 103;

// A window system that never touches the desktop: windows are rows in a map and every action is
// only recorded.
class FakeSystem final : public WindowSystem {
 public:
  struct Row {
    WindowInfo info;
    bool taskbar = true;
    bool iconic = false;
    QString uniqueId = QStringLiteral("app");
    QRect rect{0, 0, 800, 600};
  };

  Row& add(WindowHandle hwnd, const QString& title = QStringLiteral("Explorer"),
           const QString& process = QStringLiteral("explorer.exe")) {
    Row& row = rows[hwnd];
    row.info.hwnd = hwnd;
    row.info.title = title;
    row.info.className = QStringLiteral("TestClass");
    row.info.processName = process;
    row.info.processPath = QStringLiteral("C:\\test\\") + process;
    row.info.pid = 1;
    row.info.canMinimize = true;
    row.info.monitor = widgetMonitor;
    return row;
  }

  bool isWindow(WindowHandle hwnd) const override { return rows.count(hwnd) != 0; }
  bool isIconic(WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() && found->second.iconic;
  }
  WindowHandle foregroundWindow() const override { return foreground; }
  std::vector<WindowHandle> enumerateWindows() const override {
    std::vector<WindowHandle> result;
    for (const auto& [hwnd, row] : rows) result.push_back(hwnd);
    return result;
  }
  WindowInfo query(WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() ? found->second.info : WindowInfo{};
  }
  bool isTaskbarWindow(const WindowInfo& info) const override {
    const auto found = rows.find(info.hwnd);
    return found != rows.end() && found->second.taskbar && !info.title.trimmed().isEmpty();
  }
  bool canMinimize(WindowHandle hwnd) const override { return query(hwnd).canMinimize; }
  WindowHandle monitorOfWindow(WindowHandle) const override { return widgetMonitor; }
  QRect windowRect(WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() ? found->second.rect : QRect();
  }
  std::pair<WindowHandle, WindowHandle> resolveBaseAndFocus(WindowHandle hwnd) const override {
    return {hwnd, hwnd};
  }
  bool isOwnerRootActive(WindowHandle base) const override { return foreground == base; }
  std::optional<QString> appUniqueId(const WindowInfo& info) const override {
    const auto found = rows.find(info.hwnd);
    if (found == rows.end() || found->second.uniqueId.isEmpty()) return std::nullopt;
    return found->second.uniqueId;
  }
  IconBitmap windowIcon(WindowHandle, int size) const override {
    IconBitmap bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, '\x80');
    return bitmap;
  }

  void restoreWindow(WindowHandle hwnd) override { log << QStringLiteral("restore %1").arg(hwnd); }
  void minimizeWindow(WindowHandle hwnd) override {
    log << QStringLiteral("minimize %1").arg(hwnd);
  }
  void showWindow(WindowHandle hwnd) override { log << QStringLiteral("show %1").arg(hwnd); }
  void setForeground(WindowHandle hwnd) override {
    log << QStringLiteral("foreground %1").arg(hwnd);
  }
  void closeApplication(WindowHandle hwnd, bool force) override {
    log << QStringLiteral("%1 %2").arg(force ? "kill" : "close").arg(hwnd);
  }

  std::map<WindowHandle, Row> rows;
  WindowHandle foreground = 0;
  WindowHandle widgetMonitor = 1;
  QStringList log;
};

// Shell operations that never touch the start menu, the registry or the recycle bin.
class FakePins final : public PinSupport {
 public:
  PinMetadata resolveMetadata(const WindowInfo& info, const QString&) const override {
    return {info.title, QStringLiteral("C:\\lnk\\") + info.title + QStringLiteral(".lnk")};
  }
  IconBitmap pinnedIcon(const QString& uniqueId, const PinMetadata&, int size) const override {
    iconLoads << uniqueId;
    if (noShellIcon) return {};
    return filled(size, '\x30');
  }
  std::optional<QString> toastIconPath(const QString&) const override { return toastPath; }
  IconBitmap defaultIcon() const override { return filled(16, '\x77'); }
  IconBitmap recycleBinIcon(bool empty, int size) const override {
    return filled(size, empty ? '\x40' : '\xC0');
  }
  void launch(const QString& uniqueId, const PinMetadata& metadata, const QString& extra) override {
    launches << uniqueId + QLatin1Char('|') + extra + QLatin1Char('|') + metadata.shortcutPath;
  }
  RecycleBinInfo recycleBinInfo() const override { return bin; }
  void emptyRecycleBin() override { ++emptied; }
  std::vector<QuickAccessFolder> quickAccessFolders() const override { return folders; }

  static IconBitmap filled(int size, char value) {
    IconBitmap bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, value);
    return bitmap;
  }

  mutable QStringList iconLoads;
  QStringList launches;
  bool noShellIcon = false;
  std::optional<QString> toastPath;
  RecycleBinInfo bin;
  std::atomic<int> emptied = 0;
  std::vector<QuickAccessFolder> folders;
};

}  // namespace

// --- window tracker

class TrackerTest : public testing::Test {
 protected:
  void SetUp() override {
    system_ = std::make_shared<FakeSystem>();
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
  auto system = std::make_shared<FakeSystem>();
  system->add(kFirst);
  TaskbarIcons icons(system, /*async=*/false);
  const QPixmap pixmap = icons.icon(kFirst, 16, 1.5);
  ASSERT_FALSE(pixmap.isNull());
  EXPECT_EQ(pixmap.width(), 24);
  EXPECT_DOUBLE_EQ(pixmap.devicePixelRatio(), 1.5);
  EXPECT_TRUE(icons.icon(kFirst, 0, 1.0).isNull());
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

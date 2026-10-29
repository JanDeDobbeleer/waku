#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <map>
#include <nlohmann/json.hpp>

#include "core/bar/theme_class.h"
#include "core/config/loader.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/widget_context.h"
#include "platform/app_identity.h"
#include "platform/dwm_thumbnail.h"
#include "platform/pin_support.h"
#include "platform/window_system.h"
#include "platform/window_tracker.h"
#include "widgets/taskbar/pin_store.h"
#include "widgets/taskbar/recycle_bin_monitor.h"
#include "widgets/taskbar/taskbar_drop.h"
#include "widgets/taskbar/taskbar_icons.h"
#include "widgets/taskbar/taskbar_menu.h"
#include "widgets/taskbar/taskbar_preview.h"
#include "widgets/taskbar/taskbar_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

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

class FakeThumbnail final : public ThumbnailHandle {
 public:
  explicit FakeThumbnail(QSize size) : size_(size) {}
  std::optional<QSize> sourceSize() const override { return size_; }
  bool show(const QSize&) override { return true; }

 private:
  QSize size_;
};

class FakeThumbnails final : public ThumbnailBackend {
 public:
  std::unique_ptr<ThumbnailHandle> registerThumbnail(WindowHandle, WindowHandle source) override {
    ++registered;
    if (refuse.count(source)) return nullptr;
    return std::make_unique<FakeThumbnail>(QSize(1600, 900));
  }
  int registered = 0;
  std::set<WindowHandle> refuse;
};

class FakeLivePreview final : public LivePreviewBackend {
 public:
  explicit FakeLivePreview(bool available) : available_(available) {}
  bool isAvailable() const override { return available_; }
  bool activate(bool on, WindowHandle peek, WindowHandle) override {
    calls << QStringLiteral("%1 %2").arg(on ? "on" : "off").arg(peek);
    return true;
  }
  bool excludeFromPeek(WindowHandle) override { return true; }
  QStringList calls;

 private:
  bool available_;
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

yasb::config::options::TaskbarConfig taskbarOptions(const json& options) {
  const json config = {
      {"bars", {{"main", {{"widgets", {{"left", {"taskbar"}}}}}}}},
      {"widgets", {{"taskbar", {{"type", "yasb.taskbar.TaskbarWidget"}, {"options", options}}}}}};
  const auto loaded = yasb::config::loadConfigText(config.dump());
  EXPECT_TRUE(loaded.ok()) << loaded.message;
  auto typed = loaded.widgetOptions<yasb::config::options::TaskbarConfig>("taskbar");
  EXPECT_TRUE(typed.has_value());
  return typed.value_or(yasb::config::options::TaskbarConfig{});
}

class TaskbarTest : public testing::Test {
 protected:
  void SetUp() override {
    system_ = std::make_shared<FakeSystem>();
    thumbnails_ = std::make_shared<FakeThumbnails>();
    pins_ = std::make_shared<FakePins>();
  }

  void TearDown() override {
    widget_.reset();
    tracker_.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  // Builds the widget with a real tracker over the fake system. `showWidget` connects them.
  TaskbarWidget& build(const json& options, bool withTracker = true) {
    if (withTracker) tracker_ = std::make_unique<WindowTracker>(system_);
    TaskbarDeps deps;
    deps.system = system_;
    deps.tracker = tracker_.get();
    deps.icons = new TaskbarIcons(system_, /*async=*/false);
    deps.thumbnails = thumbnails_;
    deps.livePreview = live_;
    // Never the real %LOCALAPPDATA%\\YASB\\taskbar_pinned.json.
    deps.pins = pins_;
    deps.pinStore = sharedStore_;
    deps.pinFile = pinFile();
    deps.asyncRecycleBin = false;
    WidgetContext context{
        "taskbar",       "yasb.taskbar.TaskbarWidget", options, {}, {}, eventBus_, globalState_,
        pollingWorkers_, taskbarOptions(options)};
    widget_.reset(new TaskbarWidget(context, std::move(deps)));
    widget_->setDevicePixelRatio(1.0);
    widget_->setInitialPopulation(false);
    return *widget_;
  }

  static QString classOf(const QWidget* widget) { return widget->property("class").toString(); }

  QString pinFile() const { return pinDir_.filePath(QStringLiteral("taskbar_pinned.json")); }

  // Writes a taskbar_pinned.json with the given (id, title) pairs, in that order.
  void writePinned(const std::vector<std::pair<QString, QString>>& apps) {
    QJsonObject map;
    QJsonArray order;
    for (const auto& [id, title] : apps) {
      map[id] = QJsonObject{{"title", title}, {"shortcut_path", "C:\\lnk\\" + title + ".lnk"}};
      order.append(id);
    }
    QFile file(pinFile());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"pinned_apps", map}, {"pinned_order", order}}).toJson());
  }

  QJsonObject readPinned() const {
    QFile file(pinFile());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
  }

  void addTo(TaskbarWidget& taskbar, WindowHandle hwnd) {
    taskbar.onWindowAdded(hwnd, system_->query(hwnd));
  }

  QTemporaryDir pinDir_;
  PinStore* sharedStore_ = nullptr;
  std::shared_ptr<FakePins> pins_;
  std::shared_ptr<FakeSystem> system_;
  std::shared_ptr<FakeThumbnails> thumbnails_;
  std::shared_ptr<FakeLivePreview> live_ = std::make_shared<FakeLivePreview>(true);
  std::unique_ptr<WindowTracker> tracker_;
  std::unique_ptr<TaskbarWidget> widget_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

}  // namespace

// --- test_taskbar_animation.py

class TaskbarAnimationTest : public TaskbarTest, public testing::WithParamInterface<int> {};

TEST_P(TaskbarAnimationTest, WindowReturnsAfterItsGroupClosedMidAnimation) {
  system_->add(kFirst);
  system_->add(kSecond);
  TaskbarWidget& taskbar = build({{"grouping", {{"enabled", true}}}}, false);
  addTo(taskbar, kFirst);
  addTo(taskbar, kSecond);
  ASSERT_NE(taskbar.buttonFor(kSecond), nullptr);
  EXPECT_EQ(taskbar.buttonFor(kSecond), taskbar.buttonFor(kFirst));

  // FIRST leaves the group while its grow animation still runs, then the last member removes the
  // shared button immediately, which deletes that animation with it.
  taskbar.onWindowRemoved(kFirst, system_->query(kFirst));
  if (GetParam() == 0) {
    WindowInfo emptied = system_->query(kSecond);
    emptied.title.clear();
    taskbar.onWindowUpdated(kSecond, emptied);
  } else {
    WindowInfo noProcess = system_->query(kSecond);
    noProcess.processName.reset();
    taskbar.onWindowRemoved(kSecond, noProcess);
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  addTo(taskbar, kFirst);
  EXPECT_TRUE(taskbar.hasWindow(kFirst));
}

INSTANTIATE_TEST_SUITE_P(DropVariants, TaskbarAnimationTest, testing::Values(0, 1));

// --- grouping and the count badge

TEST_F(TaskbarTest, ThreeWindowsOfOneAppShareOneButtonWithCountThree) {
  for (WindowHandle hwnd : {kFirst, kSecond, kThird}) system_->add(hwnd);
  TaskbarWidget& taskbar = build({{"grouping", {{"enabled", true}, {"show_count", true}}}}, false);
  for (WindowHandle hwnd : {kFirst, kSecond, kThird}) addTo(taskbar, hwnd);

  const auto buttons = taskbar.buttons();
  ASSERT_EQ(buttons.size(), 1u);
  EXPECT_EQ(buttons[0]->countLabel->text(), QStringLiteral("3"));
  EXPECT_FALSE(buttons[0]->countLabel->isHidden());
  EXPECT_EQ(classOf(buttons[0]), QStringLiteral("app-container grouped running"));
  EXPECT_EQ(taskbar.groupMembers(QStringLiteral("app")).size(), 3u);

  taskbar.onWindowRemoved(kSecond, system_->query(kSecond));
  taskbar.onWindowRemoved(kThird, system_->query(kThird));
  EXPECT_EQ(buttons[0]->countLabel->text(), QStringLiteral("1"));
  EXPECT_TRUE(buttons[0]->countLabel->isHidden());
  EXPECT_EQ(classOf(buttons[0]), QStringLiteral("app-container running"));
}

TEST_F(TaskbarTest, WithoutGroupingEveryWindowGetsItsOwnButton) {
  for (WindowHandle hwnd : {kFirst, kSecond}) system_->add(hwnd);
  TaskbarWidget& taskbar = build({{"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);
  addTo(taskbar, kSecond);
  EXPECT_EQ(taskbar.buttons().size(), 2u);
  EXPECT_EQ(taskbar.buttons()[0]->countLabel, nullptr);
}

TEST_F(TaskbarTest, DifferentAppsStaySeparateWhenGrouped) {
  system_->add(kFirst).uniqueId = QStringLiteral("a");
  system_->add(kSecond).uniqueId = QStringLiteral("b");
  TaskbarWidget& taskbar = build({{"grouping", {{"enabled", true}}}}, false);
  addTo(taskbar, kFirst);
  addTo(taskbar, kSecond);
  EXPECT_EQ(taskbar.buttons().size(), 2u);
}

TEST_F(TaskbarTest, RemovingWithAnimationDisabledDeletesTheButtonAtOnce) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);
  ASSERT_EQ(taskbar.buttons().size(), 1u);
  taskbar.onWindowRemoved(kFirst, system_->query(kFirst));
  EXPECT_TRUE(taskbar.buttons().empty());
  EXPECT_FALSE(taskbar.hasWindow(kFirst));
}

TEST_F(TaskbarTest, AnimatedAddGrowsTheButtonAndInitialPopulationDoesNot) {
  system_->add(kFirst);
  system_->add(kSecond);
  TaskbarWidget& taskbar = build(json::object(), false);
  taskbar.setInitialPopulation(true);
  addTo(taskbar, kFirst);
  EXPECT_FALSE(taskbar.animating(kFirst));
  taskbar.setInitialPopulation(false);
  addTo(taskbar, kSecond);
  EXPECT_TRUE(taskbar.animating(kSecond));
  EXPECT_EQ(taskbar.buttonFor(kSecond)->maximumWidth(), 0);
}

// --- filtering

TEST_F(TaskbarTest, FiltersByTitleProcessClassAndMinimizability) {
  system_->add(kFirst, QStringLiteral("Keep"), QStringLiteral("keep.exe"));
  system_->add(kSecond, QStringLiteral("Hidden Title"), QStringLiteral("a.exe"));
  system_->add(kThird, QStringLiteral("Other"), QStringLiteral("skip.exe"));
  system_->add(104, QStringLiteral("Dialog"), QStringLiteral("d.exe")).info.canMinimize = false;
  system_->add(105, QStringLiteral("  "), QStringLiteral("blank.exe"));
  system_->add(106, QStringLiteral("Classy"), QStringLiteral("c.exe")).info.className =
      QStringLiteral("IgnoreMe");
  TaskbarWidget& taskbar = build(
      {{"ignore_apps",
        {{"titles", {"Hidden Title"}}, {"processes", {"skip.exe"}}, {"classes", {"IgnoreMe"}}}}},
      false);
  for (WindowHandle hwnd :
       {kFirst, kSecond, kThird, WindowHandle(104), WindowHandle(105), WindowHandle(106)})
    addTo(taskbar, hwnd);
  EXPECT_TRUE(taskbar.hasWindow(kFirst));
  EXPECT_FALSE(taskbar.hasWindow(kSecond));
  EXPECT_FALSE(taskbar.hasWindow(kThird));
  EXPECT_FALSE(taskbar.hasWindow(104));
  EXPECT_FALSE(taskbar.hasWindow(105));
  EXPECT_FALSE(taskbar.hasWindow(106));
}

TEST_F(TaskbarTest, StrictFilteringOffKeepsWindowsWithoutMinimizeBox) {
  system_->add(kFirst).info.canMinimize = false;
  TaskbarWidget& taskbar = build({{"strict_filtering", false}}, false);
  addTo(taskbar, kFirst);
  EXPECT_TRUE(taskbar.hasWindow(kFirst));
}

TEST_F(TaskbarTest, CloakedWindowOnAnotherDesktopHiddenOnlyWhenFilteringIsOn) {
  system_->add(kFirst).info.isCloaked = true;
  TaskbarWidget& visibleOnly = build({{"show_only_visible", true}}, false);
  addTo(visibleOnly, kFirst);
  EXPECT_FALSE(visibleOnly.hasWindow(kFirst));
  widget_.reset();

  TaskbarWidget& everything = build({{"show_only_visible", false}}, false);
  addTo(everything, kFirst);
  EXPECT_TRUE(everything.hasWindow(kFirst));
}

TEST_F(TaskbarTest, UpdateThatNoLongerQualifiesRemovesTheButtonAndAgainAddsIt) {
  system_->add(kFirst, QStringLiteral("Named"));
  TaskbarWidget& taskbar = build({{"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);
  WindowInfo blank = system_->query(kFirst);
  blank.title.clear();
  taskbar.onWindowUpdated(kFirst, blank);
  EXPECT_FALSE(taskbar.hasWindow(kFirst));
  taskbar.onWindowUpdated(kFirst, system_->query(kFirst));
  EXPECT_TRUE(taskbar.hasWindow(kFirst));
}

TEST_F(TaskbarTest, MonitorExclusiveShowsOnlyWindowsOnTheWidgetsMonitor) {
  system_->add(kFirst).info.monitor = 7;
  system_->add(kSecond);
  system_->add(kThird).info.monitor = 0;
  TaskbarWidget& taskbar = build({{"monitor_exclusive", true}}, false);
  taskbar.show();
  addTo(taskbar, kFirst);
  addTo(taskbar, kSecond);
  addTo(taskbar, kThird);
  EXPECT_FALSE(taskbar.hasWindow(kFirst));
  EXPECT_TRUE(taskbar.hasWindow(kSecond));
  // Unknown (transient) monitor: not added, but kept if already shown.
  EXPECT_FALSE(taskbar.hasWindow(kThird));

  system_->rows[kSecond].info.monitor = 0;
  taskbar.onWindowUpdated(kSecond, system_->query(kSecond));
  EXPECT_TRUE(taskbar.hasWindow(kSecond));
  system_->rows[kSecond].info.monitor = 9;
  taskbar.onWindowMonitorChanged(kSecond, system_->query(kSecond));
  EXPECT_FALSE(taskbar.hasWindow(kSecond));
}

// --- hide_empty

TEST_F(TaskbarTest, HideEmptyHidesTheWidgetWithoutWindowsAndShowsItAgain) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"hide_empty", true}, {"animation", {{"enabled", false}}}});
  taskbar.show();
  QTest::qWait(10);
  // The one fake window is enumerated by the tracker, so start with it gone.
  EXPECT_TRUE(taskbar.hasWindow(kFirst));
  EXPECT_TRUE(taskbar.isVisible());

  taskbar.onWindowRemoved(kFirst, system_->query(kFirst));
  EXPECT_TRUE(taskbar.isHidden());
  taskbar.onWindowAdded(kFirst, system_->query(kFirst));
  EXPECT_FALSE(taskbar.isHidden());
}

TEST_F(TaskbarTest, HideEmptyHidesAnInitiallyEmptyWidget) {
  TaskbarWidget& taskbar = build({{"hide_empty", true}});
  taskbar.show();
  QTest::qWait(10);
  EXPECT_TRUE(taskbar.isHidden());
}

// --- classes: foreground, minimized, flashing

TEST_F(TaskbarTest, ActivationAndMinimizeMoveTheButtonClass) {
  system_->add(kFirst);
  system_->add(kSecond, QStringLiteral("Other"), QStringLiteral("other.exe")).uniqueId =
      QStringLiteral("other");
  TaskbarWidget& taskbar = build({{"animation", {{"enabled", false}}}});
  taskbar.show();
  EXPECT_EQ(classOf(taskbar.buttonFor(kFirst)), QStringLiteral("app-container running"));

  tracker_->handleShellHook(shell::kWindowActivated, kFirst);
  EXPECT_EQ(classOf(taskbar.buttonFor(kFirst)), QStringLiteral("app-container foreground"));
  EXPECT_EQ(classOf(taskbar.buttonFor(kSecond)), QStringLiteral("app-container running"));

  tracker_->handleShellHook(shell::kWindowActivated, kSecond);
  EXPECT_EQ(classOf(taskbar.buttonFor(kFirst)), QStringLiteral("app-container running"));
  EXPECT_EQ(classOf(taskbar.buttonFor(kSecond)), QStringLiteral("app-container foreground"));

  system_->rows[kSecond].iconic = true;
  taskbar.onWindowMinimizeChanged(kSecond, true);
  EXPECT_EQ(classOf(taskbar.buttonFor(kSecond)), QStringLiteral("app-container running minimized"));
}

TEST_F(TaskbarTest, FlashingBlinksTenTimesThenStaysFlashingUntilActivated) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"animation", {{"enabled", false}}}});
  taskbar.show();
  tracker_->handleShellHook(shell::kFlash, kFirst);
  AppButton* button = taskbar.buttonFor(kFirst);
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(classOf(button), QStringLiteral("app-container flashing"));
  EXPECT_EQ(taskbar.flashTimerCount(), 1);

  // One tick turns the blink off, the next on again (timer interval is a second; drive it).
  const auto timers = taskbar.findChildren<FlashTimer*>();
  ASSERT_EQ(timers.size(), 1);
  QMetaObject::invokeMethod(timers[0], "timeout", Qt::DirectConnection);
  EXPECT_EQ(classOf(button), QStringLiteral("app-container running"));
  QMetaObject::invokeMethod(timers[0], "timeout", Qt::DirectConnection);
  EXPECT_EQ(classOf(button), QStringLiteral("app-container flashing"));
  for (int i = 0; i < 8; ++i) QMetaObject::invokeMethod(timers[0], "timeout", Qt::DirectConnection);
  EXPECT_EQ(classOf(button), QStringLiteral("app-container flashing"));
  EXPECT_TRUE(button->blinked);
  EXPECT_EQ(taskbar.flashTimerCount(), 0);

  tracker_->handleShellHook(shell::kWindowActivated, kFirst);
  EXPECT_EQ(classOf(button), QStringLiteral("app-container foreground"));
  EXPECT_FALSE(button->blinked);
}

// --- title label

TEST_F(TaskbarTest, TitleIsClippedAndPaddedByCodePoints) {
  system_->add(kFirst, QStringLiteral("0123456789ABCDEFG"));
  system_->add(kSecond, QStringLiteral("Hi"));
  TaskbarWidget& taskbar =
      build({{"title_label",
              {{"enabled", true}, {"show", "always"}, {"min_length", 6}, {"max_length", 10}}},
             {"animation", {{"enabled", false}}}},
            false);
  addTo(taskbar, kFirst);
  system_->rows[kSecond].uniqueId = QStringLiteral("two");
  addTo(taskbar, kSecond);
  EXPECT_EQ(taskbar.buttonFor(kFirst)->titleLabel->text(), QStringLiteral("0123456789.."));
  EXPECT_EQ(taskbar.buttonFor(kSecond)->titleLabel->text(), QStringLiteral("Hi    "));
}

TEST_F(TaskbarTest, FocusedTitleFollowsTheForegroundWindow) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"title_label", {{"enabled", true}, {"show", "focused"}}},
                                  {"animation", {{"enabled", false}}}},
                                 false);
  addTo(taskbar, kFirst);
  AppButton* button = taskbar.buttonFor(kFirst);
  EXPECT_FALSE(button->titleWrapper->property("target_visible").toBool());
  system_->foreground = kFirst;
  taskbar.onWindowMinimizeChanged(kFirst, false);
  EXPECT_TRUE(button->titleWrapper->property("target_visible").toBool());
  system_->rows[kFirst].iconic = true;
  taskbar.onWindowMinimizeChanged(kFirst, true);
  EXPECT_FALSE(button->titleWrapper->property("target_visible").toBool());
}

// --- activation

TEST_F(TaskbarTest, ClickActivatesMinimizesAndRestores) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);

  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  EXPECT_EQ(system_->log, (QStringList{"show 101", "foreground 101"}));

  system_->log.clear();
  system_->foreground = kFirst;
  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  EXPECT_EQ(system_->log, (QStringList{"minimize 101"}));

  system_->log.clear();
  system_->rows[kFirst].iconic = true;
  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  EXPECT_EQ(system_->log, (QStringList{"restore 101", "foreground 101"}));
  QTest::qWait(10);
  EXPECT_EQ(system_->log.last(), QStringLiteral("foreground 101"));
  EXPECT_EQ(system_->log.size(), 3);
}

TEST_F(TaskbarTest, ClickOnAGroupCyclesWithoutPreview) {
  for (WindowHandle hwnd : {kFirst, kSecond}) system_->add(hwnd);
  TaskbarWidget& taskbar =
      build({{"grouping", {{"enabled", true}}}, {"animation", {{"enabled", false}}}});
  taskbar.show();
  tracker_->handleShellHook(shell::kWindowActivated, kFirst);
  system_->log.clear();
  taskbar.onButtonClicked(taskbar.buttonFor(kFirst)->hwnd(), taskbar.buttonFor(kFirst));
  // The focused window leads the group, the next one is activated.
  EXPECT_EQ(system_->log, (QStringList{"show 102", "foreground 102"}));
}

TEST_F(TaskbarTest, ContextMenuClosesAndKillsThroughTheWindowSystem) {
  for (WindowHandle hwnd : {kFirst, kSecond}) system_->add(hwnd);
  TaskbarWidget& taskbar =
      build({{"grouping", {{"enabled", true}}}, {"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);
  addTo(taskbar, kSecond);
  taskbar.show();

  QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  EXPECT_TRUE(taskbar.contextMenuOpen());
  QStringList titles;
  QList<QAction*> actions;
  for (QAction* action : menu->actions()) {
    if (action->isSeparator()) continue;
    titles << action->text();
    actions << action;
  }
  EXPECT_EQ(titles,
            (QStringList{"Pin to taskbar", "End task", "Close window", "Close all windows"}));

  actions[1]->trigger();
  actions[2]->trigger();
  actions[3]->trigger();
  EXPECT_EQ(system_->log, (QStringList{"kill 101", "close 101", "close 101", "close 102"}));
  menu->hide();
  EXPECT_FALSE(taskbar.contextMenuOpen());
}

// --- icons

TEST_F(TaskbarTest, ButtonShowsTheWindowIconAtTheConfiguredSize) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"icon_size", 20}, {"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);
  const QLabel* icon = taskbar.buttonFor(kFirst)->iconLabel;
  ASSERT_NE(icon, nullptr);
  EXPECT_EQ(icon->size(), QSize(20, 20));
  EXPECT_FALSE(icon->pixmap().isNull());
  EXPECT_EQ(icon->pixmap().width(), 20);
}

TEST_F(TaskbarTest, ZeroIconSizeBuildsNoIconLabel) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build({{"icon_size", 0}, {"animation", {{"enabled", false}}}}, false);
  addTo(taskbar, kFirst);
  EXPECT_EQ(taskbar.buttonFor(kFirst)->iconLabel, nullptr);
}

// --- preview

TEST(TaskbarPreviewLayout, GridColumnsShrinkToTheMinimumCellWidth) {
  // 1920 wide, margin 8, padding 8: (1920 - 16 - 16 + 16) / (100 + 16) = 16 cells.
  EXPECT_EQ(previewGridColumns(3, 1920, 8, 8), 3);
  EXPECT_EQ(previewGridColumns(40, 1920, 8, 8), 16);
  EXPECT_EQ(previewGridColumns(5, 200, 8, 8), 1);
  EXPECT_EQ(ceilDiv(7, 3), 3);
  EXPECT_EQ(ceilDiv(6, 3), 2);
}

class TaskbarPreviewTest : public TaskbarTest {
 protected:
  TaskbarWidget& buildWithPreview(bool peek = true) {
    for (WindowHandle hwnd : {kFirst, kSecond}) system_->add(hwnd);
    TaskbarWidget& taskbar =
        build({{"grouping", {{"enabled", true}}},
               {"animation", {{"enabled", false}}},
               {"preview", {{"enabled", true}, {"width", 180}, {"peek", peek}}}},
              false);
    addTo(taskbar, kFirst);
    addTo(taskbar, kSecond);
    taskbar.resize(300, 30);
    taskbar.move(500, 700);
    taskbar.show();
    return taskbar;
  }
};

TEST_F(TaskbarPreviewTest, GroupClickOpensAThumbnailPerWindow) {
  TaskbarWidget& taskbar = buildWithPreview();
  AppButton* button = taskbar.buttonFor(kFirst);
  taskbar.onButtonClicked(kFirst, button);

  PreviewManager* manager = taskbar.previewManager();
  ASSERT_NE(manager, nullptr);
  PreviewPopup* popup = manager->popup();
  ASSERT_NE(popup, nullptr);
  EXPECT_FALSE(popup->listMode());
  EXPECT_EQ(popup->items().size(), 2u);
  EXPECT_EQ(thumbnails_->registered, 2);
  EXPECT_EQ(popup->srcHwnd(), button->hwnd());
  for (const PreviewItem* item : popup->items()) {
    EXPECT_TRUE(item->thumbnail != nullptr);
    EXPECT_GE(item->thumbRect().width(), 1);
    // 1600x900 source fitted into the 180 wide, 16:9 box (101 high), truncated like Python's int().
    EXPECT_NEAR(item->thumbRect().width(), 180, 1);
    EXPECT_NEAR(item->thumbRect().height(), 101, 1);
  }
  // Items tile the popup: the first starts at 0 and the last reaches the right edge.
  EXPECT_EQ(popup->items().front()->geometry().left(), 0);
  EXPECT_EQ(popup->items().back()->geometry().right() + 1, popup->width());

  manager->hidePreview();
  EXPECT_EQ(manager->popup(), nullptr);
}

TEST_F(TaskbarPreviewTest, PeekFadesTheDesktopAfterTheDelayOnlyWhileThePointerIsOnThePreview) {
  TaskbarWidget& taskbar = buildWithPreview();
  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  PreviewManager* manager = taskbar.previewManager();
  ASSERT_TRUE(manager->peekEnabled());
  ASSERT_EQ(live_->calls, (QStringList{"off 0"}));

  bool onPreview = false;
  manager->setPointerOnPreview([&] { return onPreview; });
  manager->setPeek(kSecond, true);
  QTest::qWait(PreviewManager::kPeekDelayMs + 100);
  EXPECT_EQ(manager->peeking(), 0u);

  onPreview = true;
  manager->setPeek(kSecond, true);
  QTest::qWait(PreviewManager::kPeekDelayMs + 100);
  EXPECT_EQ(manager->peeking(), kSecond);
  EXPECT_EQ(live_->calls.last(), QStringLiteral("on 102"));

  // Moving to another window switches at once, leaving restores the desktop after a beat.
  manager->setPeek(kFirst, true);
  EXPECT_EQ(manager->peeking(), kFirst);
  manager->setPeek(kFirst, false);
  QTest::qWait(150);
  EXPECT_EQ(manager->peeking(), 0u);
  EXPECT_EQ(live_->calls.last(), QStringLiteral("off 0"));

  // Closing the preview always releases the desktop.
  manager->setPeek(kFirst, true);
  QTest::qWait(PreviewManager::kPeekDelayMs + 100);
  EXPECT_EQ(manager->peeking(), kFirst);
  manager->hidePreview();
  EXPECT_EQ(manager->peeking(), 0u);
}

TEST_F(TaskbarPreviewTest, MissingPeekExportDisablesPeekButPreviewStillWorks) {
  live_ = std::make_shared<FakeLivePreview>(false);
  TaskbarWidget& taskbar = buildWithPreview(/*peek=*/true);
  PreviewManager* manager = taskbar.previewManager();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->peekEnabled());

  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  ASSERT_NE(manager->popup(), nullptr);
  EXPECT_EQ(manager->popup()->items().size(), 2u);
  manager->setPeek(kSecond, true);
  QTest::qWait(PreviewManager::kPeekDelayMs + 100);
  EXPECT_EQ(manager->peeking(), 0u);
  EXPECT_TRUE(live_->calls.isEmpty());
}

TEST_F(TaskbarPreviewTest, WindowThatDiesBeforeRegistrationLosesItsSlot) {
  TaskbarWidget& taskbar = buildWithPreview();
  thumbnails_->refuse.insert(kSecond);
  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  PreviewManager* manager = taskbar.previewManager();
  ASSERT_NE(manager->popup(), nullptr);
  EXPECT_EQ(manager->popup()->items().size(), 1u);
  EXPECT_EQ(manager->popup()->items()[0]->hwnd(), kFirst);
}

TEST_F(TaskbarPreviewTest, ManyWindowsFallBackToAListWithoutThumbnails) {
  for (WindowHandle hwnd = 200; hwnd < 230; ++hwnd)
    system_->add(hwnd, QStringLiteral("Window %1").arg(hwnd));
  TaskbarWidget& taskbar = buildWithPreview();
  PreviewManager* manager = taskbar.previewManager();
  std::vector<WindowHandle> all;
  for (WindowHandle hwnd = 200; hwnd < 230; ++hwnd) all.push_back(hwnd);
  manager->showPreviewForHwnds(all, taskbar.buttonFor(kFirst));
  PreviewPopup* popup = manager->popup();
  ASSERT_NE(popup, nullptr);
  EXPECT_TRUE(popup->listMode());
  EXPECT_EQ(thumbnails_->registered, 0);
  EXPECT_EQ(popup->items().size(), all.size());
}

TEST_F(TaskbarPreviewTest, ClosingFromTheListLeavesTheOtherWindows) {
  TaskbarWidget& taskbar = buildWithPreview();
  taskbar.onButtonClicked(kFirst, taskbar.buttonFor(kFirst));
  PreviewPopup* popup = taskbar.previewManager()->popup();
  ASSERT_NE(popup, nullptr);
  popup->closeWindow(kFirst);
  EXPECT_EQ(system_->log, (QStringList{"close 101"}));
  system_->rows.erase(kFirst);
  QTest::qWait(20);
  ASSERT_NE(taskbar.previewManager()->popup(), nullptr);
  EXPECT_EQ(taskbar.previewManager()->popup()->items().size(), 1u);
}

// --- pinned apps (PinManager, TaskbarDropWidget, app_menu)

namespace {

constexpr const char* kRecycleBinId = "explorer:::{645FF040-5081-101B-9F08-00AA002F954E}";

QList<QAction*> actionsOf(QMenu* menu) {
  QList<QAction*> actions;
  for (QAction* action : menu->actions()) {
    if (!action->isSeparator()) actions << action;
  }
  return actions;
}

QStringList titlesOf(QMenu* menu) {
  QStringList titles;
  for (QAction* action : actionsOf(menu)) titles << action->text();
  return titles;
}

class TaskbarPinTest : public TaskbarTest {
 protected:
  void TearDown() override {
    second_.reset();
    TaskbarTest::TearDown();
    store_.reset();
  }

  static json plain() {
    return {{"animation", {{"enabled", false}}}, {"grouping", {{"enabled", true}}}};
  }

  // A taskbar over the pinned file written from `apps`, shown (so its pinned buttons exist).
  TaskbarWidget& buildPinned(const std::vector<std::pair<QString, QString>>& apps,
                             const json& options = plain()) {
    writePinned(apps);
    TaskbarWidget& taskbar = build(options, false);
    taskbar.resize(400, 30);
    taskbar.show();
    QCoreApplication::processEvents();
    return taskbar;
  }

  // Two taskbars over one shared store, as the app wires them. The first is widget_.
  TaskbarWidget& buildShared(const std::vector<std::pair<QString, QString>>& apps) {
    writePinned(apps);
    store_ = std::make_unique<PinStore>(pinFile());
    sharedStore_ = store_.get();
    TaskbarWidget& first = build(plain(), false);
    first.show();

    TaskbarDeps deps;
    deps.system = system_;
    deps.icons = new TaskbarIcons(system_, /*async=*/false);
    deps.pins = pins_;
    deps.pinStore = sharedStore_;
    const json options = plain();
    WidgetContext context{
        "taskbar2",      "yasb.taskbar.TaskbarWidget", options, {}, {}, eventBus_, globalState_,
        pollingWorkers_, taskbarOptions(options)};
    second_.reset(new TaskbarWidget(context, std::move(deps)));
    second_->setDevicePixelRatio(1.0);
    second_->show();
    return first;
  }

  static QString pinnedId(const QJsonValue& value) { return value.toString(); }

  std::unique_ptr<PinStore> store_;
  std::unique_ptr<TaskbarWidget> second_;
};

}  // namespace

TEST_F(TaskbarPinTest, PinnedAppsShowAsButtonsInTheStoredOrder) {
  TaskbarWidget& taskbar = buildPinned({{"app-a", "A"}, {"app-b", "B"}});
  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"app-a", "app-b"}));

  AppButton* a = taskbar.pinnedOnlyButton(QStringLiteral("app-a"));
  ASSERT_NE(a, nullptr);
  EXPECT_TRUE(isPseudoHwnd(a->hwnd()));
  EXPECT_EQ(classOf(a), QStringLiteral("app-container"));
  EXPECT_TRUE(a->property("pinned").toBool());
  EXPECT_EQ(a->titleLabel, nullptr);
  ASSERT_NE(a->iconLabel, nullptr);
  EXPECT_FALSE(a->iconLabel->pixmap().isNull());

  // Clicking a pinned app that does not run launches it, with the stored shortcut.
  taskbar.onButtonClicked(a->hwnd(), a);
  EXPECT_EQ(pins_->launches, (QStringList{"app-a||C:\\lnk\\A.lnk"}));
  // Nothing was asked of the window system.
  EXPECT_TRUE(system_->log.isEmpty());
}

TEST_F(TaskbarPinTest, TheRealFileIsNeverTouched) {
  // The fixture points every taskbar at a temp file; a missing file means nothing is pinned.
  TaskbarWidget& taskbar = build(plain(), false);
  taskbar.show();
  EXPECT_TRUE(taskbar.pinStore()->isEmpty());
  EXPECT_EQ(taskbar.pinStore()->filePath(), pinFile());
}

TEST_F(TaskbarPinTest, ARunningPinnedAppTakesItsPinnedButtonsPlace) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}, {"b", "B"}, {"c", "C"}});
  system_->add(kFirst).uniqueId = QStringLiteral("b");
  addTo(taskbar, kFirst);

  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"a", "b", "c"}));
  EXPECT_EQ(taskbar.pinnedOnlyButton(QStringLiteral("b")), nullptr);
  AppButton* running = taskbar.buttonFor(kFirst);
  ASSERT_NE(running, nullptr);
  EXPECT_TRUE(taskbar.isAppPinned(kFirst));
  EXPECT_TRUE(classOf(running).contains(QLatin1String("pinned")));
  EXPECT_TRUE(running->property("pinned").toBool());
}

TEST_F(TaskbarPinTest, ClosingAPinnedAppBringsItsButtonBackWhereItWas) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}, {"b", "B"}, {"c", "C"}});
  system_->add(kFirst).uniqueId = QStringLiteral("b");
  addTo(taskbar, kFirst);

  taskbar.onWindowRemoved(kFirst, system_->query(kFirst));
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"a", "b", "c"}));
  EXPECT_NE(taskbar.pinnedOnlyButton(QStringLiteral("b")), nullptr);
  EXPECT_FALSE(taskbar.isAppPinned(kFirst));
}

TEST_F(TaskbarPinTest, ASecondWindowKeepsThePinnedButtonAwayWhileTheAppRuns) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}});
  system_->add(kFirst).uniqueId = QStringLiteral("a");
  system_->add(kSecond).uniqueId = QStringLiteral("a");
  addTo(taskbar, kFirst);
  addTo(taskbar, kSecond);
  EXPECT_EQ(taskbar.buttons().size(), 1u);

  taskbar.onWindowRemoved(kFirst, system_->query(kFirst));
  QCoreApplication::processEvents();
  EXPECT_EQ(taskbar.pinnedOnlyButton(QStringLiteral("a")), nullptr);
  EXPECT_EQ(taskbar.buttons().size(), 1u);

  taskbar.onWindowRemoved(kSecond, system_->query(kSecond));
  QCoreApplication::processEvents();
  EXPECT_NE(taskbar.pinnedOnlyButton(QStringLiteral("a")), nullptr);
}

TEST_F(TaskbarPinTest, UnpinnedWindowsGoAfterThePinnedButtons) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}, {"b", "B"}});
  system_->add(kFirst).uniqueId = QStringLiteral("other");
  addTo(taskbar, kFirst);
  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"a", "b", ""}));
  EXPECT_FALSE(taskbar.isAppPinned(kFirst));
}

TEST_F(TaskbarPinTest, PinningARunningAppWritesTheFileAndMarksItsButton) {
  TaskbarWidget& taskbar = build(plain(), false);
  system_->add(kFirst, QStringLiteral("Notes"), QStringLiteral("notes.exe")).uniqueId =
      QStringLiteral("path:c:\\notes.exe");
  system_->add(kSecond).uniqueId = QStringLiteral("other");
  addTo(taskbar, kSecond);
  addTo(taskbar, kFirst);
  QSignalSpy spy(taskbar.pinStore(), &PinStore::pinnedAppsChanged);

  taskbar.pinApp(kFirst);

  const QJsonObject file = readPinned();
  const QJsonObject apps = file.value("pinned_apps").toObject();
  ASSERT_TRUE(apps.contains("path:c:\\notes.exe"));
  const QJsonObject entry = apps.value("path:c:\\notes.exe").toObject();
  EXPECT_EQ(entry.value("title").toString(), QStringLiteral("Notes"));
  EXPECT_EQ(entry.value("shortcut_path").toString(), QStringLiteral("C:\\lnk\\Notes.lnk"));
  EXPECT_EQ(file.value("pinned_order").toArray().size(), 1);
  EXPECT_TRUE(taskbar.isAppPinned(kFirst));
  EXPECT_TRUE(classOf(taskbar.buttonFor(kFirst)).contains(QLatin1String("pinned")));
  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.first().at(0).toString(), QStringLiteral("pin"));
  // The pinned app moved ahead of the unpinned window.
  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"path:c:\\notes.exe", ""}));

  taskbar.unpinApp(kFirst);
  EXPECT_TRUE(readPinned().value("pinned_apps").toObject().isEmpty());
  EXPECT_FALSE(taskbar.isAppPinned(kFirst));
  EXPECT_FALSE(classOf(taskbar.buttonFor(kFirst)).contains(QLatin1String("pinned")));
}

TEST_F(TaskbarPinTest, PinsAndUnpinsReachEveryTaskbar) {
  TaskbarWidget& first = buildShared({});
  system_->add(kFirst).uniqueId = QStringLiteral("app");
  addTo(first, kFirst);

  first.pinApp(kFirst);
  EXPECT_NE(second_->pinnedOnlyButton(QStringLiteral("app")), nullptr);
  EXPECT_EQ(second_->layoutUniqueIds(), (QStringList{"app"}));

  first.unpinApp(kFirst);
  EXPECT_EQ(second_->pinnedOnlyButton(QStringLiteral("app")), nullptr);
  EXPECT_TRUE(second_->layoutUniqueIds().isEmpty());
}

TEST_F(TaskbarPinTest, UnpinningFromAnotherTaskbarClearsARunningPinnedWindow) {
  TaskbarWidget& first = buildShared({{"app", "App"}});
  system_->add(kFirst).uniqueId = QStringLiteral("app");
  addTo(first, kFirst);
  ASSERT_TRUE(first.isAppPinned(kFirst));

  second_->unpinPinnedOnlyApp(QStringLiteral("app"), pseudoHwndFor(QStringLiteral("app")));
  EXPECT_FALSE(first.isAppPinned(kFirst));
  EXPECT_FALSE(classOf(first.buttonFor(kFirst)).contains(QLatin1String("pinned")));
}

TEST_F(TaskbarPinTest, ReorderingReachesEveryTaskbar) {
  TaskbarWidget& first = buildShared({{"a", "A"}, {"b", "B"}, {"c", "C"}});
  system_->add(kFirst).uniqueId = QStringLiteral("other");
  addTo(first, kFirst);
  ASSERT_EQ(first.layoutUniqueIds(), (QStringList{"a", "b", "c", ""}));

  sharedStore_->updateOrder({"c", "a", "b"});
  EXPECT_EQ(first.layoutUniqueIds(), (QStringList{"c", "a", "b", ""}));
  EXPECT_EQ(second_->layoutUniqueIds(), (QStringList{"c", "a", "b"}));
  EXPECT_EQ(readPinned().value("pinned_order").toArray().at(0).toString(), QStringLiteral("c"));
}

TEST_F(TaskbarPinTest, HideEmptyKeepsATaskbarWithPinnedAppsVisible) {
  json options = plain();
  options["hide_empty"] = true;
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}}, options);
  QCoreApplication::processEvents();
  EXPECT_TRUE(taskbar.isVisible());
}

// --- drag reorder and drop hover

TEST_F(TaskbarPinTest, DraggingAPinnedButtonReordersAndSavesTheOrder) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}, {"b", "B"}, {"c", "C"}});
  TaskbarDropHandler* drop = taskbar.dropHandler();
  AppButton* a = taskbar.pinnedOnlyButton(QStringLiteral("a"));
  AppButton* c = taskbar.pinnedOnlyButton(QStringLiteral("c"));
  ASSERT_TRUE(a && c);

  ASSERT_TRUE(drop->dragEnterInternal(a));
  EXPECT_TRUE(a->property("dragging").toBool());
  const QPoint overC = c->geometry().center();
  EXPECT_TRUE(drop->dragMoveInternal(a, overC));
  // Dragged from the left of c: it goes after c.
  EXPECT_EQ(drop->currentIndicatorIndex(), 3);
  EXPECT_TRUE(drop->dropInternal(a, overC));

  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"b", "c", "a"}));
  EXPECT_FALSE(a->property("dragging").toBool());
  const QJsonArray order = readPinned().value("pinned_order").toArray();
  ASSERT_EQ(order.size(), 3);
  EXPECT_EQ(order.at(0).toString(), QStringLiteral("b"));
  EXPECT_EQ(order.at(2).toString(), QStringLiteral("a"));
}

TEST_F(TaskbarPinTest, DraggingBackwardsPutsTheButtonBeforeTheHoveredOne) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}, {"b", "B"}, {"c", "C"}});
  TaskbarDropHandler* drop = taskbar.dropHandler();
  AppButton* c = taskbar.pinnedOnlyButton(QStringLiteral("c"));
  AppButton* a = taskbar.pinnedOnlyButton(QStringLiteral("a"));
  ASSERT_TRUE(drop->dragEnterInternal(c));
  const QPoint overA = a->geometry().center();
  EXPECT_TRUE(drop->dragMoveInternal(c, overA));
  EXPECT_EQ(drop->currentIndicatorIndex(), 0);
  EXPECT_TRUE(drop->dropInternal(c, overA));
  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"c", "a", "b"}));
}

TEST_F(TaskbarPinTest, DroppingOnTheSameSlotChangesNothing) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}, {"b", "B"}});
  TaskbarDropHandler* drop = taskbar.dropHandler();
  AppButton* a = taskbar.pinnedOnlyButton(QStringLiteral("a"));
  const QFileInfo before(pinFile());
  const QDateTime stamp = before.lastModified();

  ASSERT_TRUE(drop->dragEnterInternal(a));
  const QPoint overSelf = a->geometry().center();
  EXPECT_TRUE(drop->dragMoveInternal(a, overSelf));
  EXPECT_EQ(drop->currentIndicatorIndex(), -1);
  EXPECT_TRUE(drop->dropInternal(a, overSelf));
  EXPECT_EQ(taskbar.layoutUniqueIds(), (QStringList{"a", "b"}));
  EXPECT_EQ(QFileInfo(pinFile()).lastModified(), stamp);
}

TEST_F(TaskbarPinTest, WindowEventsAreHeldBackWhileAButtonIsDragged) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}});
  TaskbarDropHandler* drop = taskbar.dropHandler();
  AppButton* a = taskbar.pinnedOnlyButton(QStringLiteral("a"));
  system_->add(kFirst).uniqueId = QStringLiteral("other");

  ASSERT_TRUE(drop->dragEnterInternal(a));
  addTo(taskbar, kFirst);
  EXPECT_FALSE(taskbar.hasWindow(kFirst));
  drop->dragLeave();
  taskbar.setDragging(false);
  addTo(taskbar, kFirst);
  EXPECT_TRUE(taskbar.hasWindow(kFirst));
}

TEST_F(TaskbarPinTest, HoveringAFileOverAButtonRaisesItsWindowAfter200Ms) {
  system_->add(kFirst);
  TaskbarWidget& taskbar = build(plain(), false);
  addTo(taskbar, kFirst);
  taskbar.resize(300, 30);
  taskbar.show();
  QCoreApplication::processEvents();
  TaskbarDropHandler* drop = taskbar.dropHandler();
  AppButton* button = taskbar.buttonFor(kFirst);
  const QPoint over = button->geometry().center();

  drop->dragHoverExternal(over);
  EXPECT_TRUE(drop->hoverTimerActive());
  EXPECT_EQ(drop->hoverTarget(), kFirst);
  EXPECT_TRUE(system_->log.isEmpty());
  QTest::qWait(300);
  EXPECT_TRUE(system_->log.contains(QStringLiteral("foreground 101")));

  // Leaving cancels a pending raise.
  system_->log.clear();
  drop->dragHoverExternal(over);
  drop->dragLeave();
  EXPECT_FALSE(drop->hoverTimerActive());
  EXPECT_EQ(drop->hoverTarget(), 0u);
  QTest::qWait(300);
  EXPECT_TRUE(system_->log.isEmpty());

  // Dropping raises at once.
  drop->dragHoverExternal(over);
  drop->dropExternal();
  EXPECT_TRUE(system_->log.contains(QStringLiteral("foreground 101")));
}

// --- context menu

TEST_F(TaskbarPinTest, PinnedOnlyMenuOpensAndUnpins) {
  TaskbarWidget& taskbar = buildPinned({{"path:c:\\x.exe", "X"}});
  AppButton* button = taskbar.pinnedOnlyButton(QStringLiteral("path:c:\\x.exe"));
  QMenu* menu = taskbar.showContextMenu(button->hwnd(), QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(titlesOf(menu), (QStringList{"Open app", "Unpin from taskbar"}));

  actionsOf(menu)[0]->trigger();
  EXPECT_EQ(pins_->launches, (QStringList{"path:c:\\x.exe||C:\\lnk\\X.lnk"}));
  actionsOf(menu)[1]->trigger();
  EXPECT_TRUE(readPinned().value("pinned_apps").toObject().isEmpty());
  EXPECT_EQ(taskbar.pinnedOnlyButton(QStringLiteral("path:c:\\x.exe")), nullptr);
  menu->close();
}

TEST_F(TaskbarPinTest, RunningPinnedMenuOffersUnpinAndEndTask) {
  TaskbarWidget& taskbar = buildPinned({{"a", "A"}});
  system_->add(kFirst).uniqueId = QStringLiteral("a");
  addTo(taskbar, kFirst);
  QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(titlesOf(menu),
            (QStringList{"Open app", "Unpin from taskbar", "End task", "Close window"}));
  menu->close();
}

TEST_F(TaskbarPinTest, MenuPinsARunningApp) {
  TaskbarWidget& taskbar = build(plain(), false);
  system_->add(kFirst).uniqueId = QStringLiteral("app");
  addTo(taskbar, kFirst);
  QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  actionsOf(menu)[0]->trigger();
  EXPECT_TRUE(taskbar.isAppPinned(kFirst));
  EXPECT_TRUE(readPinned().value("pinned_apps").toObject().contains("app"));
  menu->close();
}

TEST_F(TaskbarPinTest, BrowserMenusLaunchTheirPrivateModes) {
  struct Case {
    QString id;
    QStringList first;
    QString privateArgs;
    QString privateTarget;
    QString firstArgs = QString();
  };
  const std::vector<Case> cases = {
      {"path:c:\\edge\\msedge.exe", {"New Window", "New InPrivate Window"}, "--inprivate", ""},
      {"path:c:\\chrome\\chrome.exe", {"New Window", "New Incognito Window"}, "--incognito", ""},
      {"path:c:\\ff\\firefox.exe",
       {"New Window", "New Private Window"},
       "-private-window",
       "",
       "-new-window"},
      {"aumid:308046B0AF4A39CB",
       {"New Window", "New Private Window"},
       "",
       "aumid:308046B0AF4A39CB;PrivateBrowsingAUMID"},
  };
  for (const Case& test : cases) {
    SCOPED_TRACE(test.id.toStdString());
    pins_->launches.clear();
    TaskbarWidget& taskbar = build(plain(), false);
    system_->rows.clear();
    system_->add(kFirst).uniqueId = test.id;
    addTo(taskbar, kFirst);
    QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
    ASSERT_NE(menu, nullptr);
    const QStringList titles = titlesOf(menu);
    ASSERT_GE(titles.size(), test.first.size());
    EXPECT_EQ(titles.mid(0, test.first.size()), test.first);

    const QList<QAction*> actions = actionsOf(menu);
    actions[0]->trigger();
    ASSERT_EQ(pins_->launches.size(), 1);
    EXPECT_EQ(pins_->launches[0], test.id + QLatin1Char('|') + test.firstArgs + QLatin1Char('|'));
    actions[1]->trigger();
    const QString expected = test.privateTarget.isEmpty() ? test.id : test.privateTarget;
    ASSERT_EQ(pins_->launches.size(), 2);
    EXPECT_EQ(pins_->launches[1],
              expected + QLatin1Char('|') + test.privateArgs + QLatin1Char('|'));
    menu->close();
  }
}

TEST_F(TaskbarPinTest, VsCodeNewWindowPassesDashN) {
  TaskbarWidget& taskbar = build(plain(), false);
  system_->add(kFirst).uniqueId = QStringLiteral("path:c:\\code\\Code.exe");
  addTo(taskbar, kFirst);
  QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  actionsOf(menu)[0]->trigger();
  EXPECT_EQ(pins_->launches, (QStringList{"path:c:\\code\\Code.exe|-n|"}));
  menu->close();
}

TEST_F(TaskbarPinTest, ExplorerMenuListsQuickAccessFoldersThenFileExplorer) {
  pins_->folders = {{"Downloads", "C:\\Users\\u\\Downloads"}, {"Docs", "C:\\Users\\u\\Docs"}};
  TaskbarWidget& taskbar = build(plain(), false);
  system_->add(kFirst, QStringLiteral("Folder"), QStringLiteral("explorer.exe")).uniqueId =
      QStringLiteral("explorer:C:\\Work");
  addTo(taskbar, kFirst);
  QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(titlesOf(menu), (QStringList{"Downloads", "Docs", "File Explorer", "Pin to taskbar",
                                         "End task", "Close window"}));
  actionsOf(menu)[0]->trigger();
  actionsOf(menu)[2]->trigger();
  EXPECT_EQ(pins_->launches, (QStringList{"explorer:C:\\Users\\u\\Downloads||",
                                          "explorer:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}||"}));
  menu->close();
}

TEST_F(TaskbarPinTest, TerminalMenuListsTheVisibleProfiles) {
  QTemporaryDir appData;
  const QString settings = appData.filePath(
      QStringLiteral("Packages/Microsoft.WindowsTerminal_8wekyb3d8bbwe/LocalState/settings.json"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(settings).absolutePath()));
  QFile file(settings);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"profiles":{"list":[{"name":"PowerShell","guid":"{aaa}"},
      {"name":"Hidden","guid":"{bbb}","hidden":true},{"name":"Ubuntu","guid":"{ccc}"}]}})");
  file.close();

  const QByteArray previous = qgetenv("LOCALAPPDATA");
  qputenv("LOCALAPPDATA", appData.path().toUtf8());
  TaskbarWidget& taskbar = build(plain(), false);
  system_->add(kFirst).uniqueId =
      QStringLiteral("aumid:Microsoft.WindowsTerminal_8wekyb3d8bbwe!App");
  addTo(taskbar, kFirst);
  QMenu* menu = taskbar.showContextMenu(kFirst, QPoint(10, 10));
  qputenv("LOCALAPPDATA", previous);
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(titlesOf(menu).mid(0, 3), (QStringList{"PowerShell", "Ubuntu", "New Window"}));
  actionsOf(menu)[1]->trigger();
  EXPECT_EQ(pins_->launches,
            (QStringList{"aumid:Microsoft.WindowsTerminal_8wekyb3d8bbwe!App|-p {ccc}|"}));
  menu->close();
}

TEST(TaskbarMenuHelpers, TerminalProfilesComeFromTheInstallsSettingsFile) {
  QTemporaryDir appData;
  const auto write = [&](const QString& relative, const QByteArray& json) {
    const QString path = appData.filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(json);
  };
  write(QStringLiteral("Packages/Microsoft.WindowsTerminal_8wekyb3d8bbwe/LocalState/settings.json"),
        R"({"profiles":{"list":[{"name":"Stable","guid":"{1}"},{"guid":"{2}"},{"name":"NoGuid"},
            {"name":"Gone","guid":"{3}","hidden":true}]}})");
  write(QStringLiteral(
            "Packages/Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe/LocalState/settings.json"),
        R"({"profiles":{"list":[{"name":"Preview","guid":"{4}"}]}})");
  write(QStringLiteral("Microsoft/Windows Terminal/settings.json"),
        R"({"profiles":{"list":[{"name":"Portable","guid":"{5}"}]}})");

  const QString dir = appData.path();
  const auto stable =
      windowsTerminalProfiles(QStringLiteral("aumid:Microsoft.WindowsTerminal"), dir);
  ASSERT_EQ(stable.size(), 2u);
  EXPECT_EQ(stable[0].name, QStringLiteral("Stable"));
  EXPECT_EQ(stable[1].name, QStringLiteral("Unknown"));
  EXPECT_EQ(stable[1].guid, QStringLiteral("{2}"));
  EXPECT_EQ(windowsTerminalProfiles(QStringLiteral("aumid:Microsoft.WindowsTerminalPreview"), dir)
                .at(0)
                .name,
            QStringLiteral("Preview"));
  EXPECT_EQ(
      windowsTerminalProfiles(QStringLiteral("path:c:\\wt\\WindowsTerminal.exe"), dir).at(0).name,
      QStringLiteral("Portable"));
  EXPECT_TRUE(windowsTerminalProfiles(QStringLiteral("aumid:Microsoft.WindowsTerminal"),
                                      dir + QStringLiteral("/missing"))
                  .empty());
}

TEST(TaskbarMenuHelpers, MenuOpensOutsideTheBarByItsPosition) {
  const QRect bar(100, 0, 800, 40);
  // Below a top bar: 6 px under it, centred on the button.
  EXPECT_EQ(contextMenuPosition(bar, QPoint(300, 20), QSize(120, 200), true), QPoint(240, 46));
  // Above a bottom bar.
  const QRect bottom(100, 1000, 800, 40);
  EXPECT_EQ(contextMenuPosition(bottom, QPoint(300, 1020), QSize(120, 200), false),
            QPoint(240, 794));
}

TEST(TaskbarMenuHelpers, SizesFormatLikeHumanize) {
  EXPECT_EQ(naturalSizeBinary(0), QStringLiteral("0 Bytes"));
  EXPECT_EQ(naturalSizeBinary(1), QStringLiteral("1 Byte"));
  EXPECT_EQ(naturalSizeBinary(12), QStringLiteral("12 Bytes"));
  EXPECT_EQ(naturalSizeBinary(1536), QStringLiteral("1.50 KiB"));
  EXPECT_EQ(naturalSizeBinary(5 * 1024 * 1024), QStringLiteral("5.00 MiB"));
}

// --- Recycle Bin

TEST_F(TaskbarPinTest, RecycleBinButtonShowsTheBinsStateAndItsMenu) {
  TaskbarWidget& taskbar = buildPinned({{kRecycleBinId, "Recycle Bin"}});
  AppButton* button = taskbar.pinnedOnlyButton(QString::fromLatin1(kRecycleBinId));
  ASSERT_NE(button, nullptr);
  ASSERT_NE(taskbar.recycleBinMonitor(), nullptr);
  EXPECT_TRUE(taskbar.recycleBinMonitor()->active());

  // The empty bin's stock icon first, the full one once the monitor sees items.
  EXPECT_EQ(qAlpha(button->iconLabel->pixmap().toImage().pixel(0, 0)), 0x40);
  pins_->bin = {1536, 3};  // size, then item count
  taskbar.recycleBinMonitor()->refresh();
  ASSERT_EQ(taskbar.recycleBinMonitor()->last().numItems, 3);
  EXPECT_EQ(qAlpha(button->iconLabel->pixmap().toImage().pixel(0, 0)), 0xC0);

  QMenu* menu = taskbar.showContextMenu(button->hwnd(), QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(titlesOf(menu),
            (QStringList{"3 items (1.50 KiB)", "Open", "Empty Recycle Bin", "Unpin from taskbar"}));
  EXPECT_FALSE(actionsOf(menu)[0]->isEnabled());
  EXPECT_TRUE(actionsOf(menu)[2]->isEnabled());
  actionsOf(menu)[1]->trigger();
  EXPECT_EQ(
      pins_->launches,
      (QStringList{"explorer:::{645FF040-5081-101B-9F08-00AA002F954E}||C:\\lnk\\Recycle Bin.lnk"}));
  actionsOf(menu)[2]->trigger();
  for (int i = 0; i < 100 && pins_->emptied == 0; ++i) QTest::qWait(20);
  EXPECT_EQ(pins_->emptied, 1);
  menu->close();

  // Unpinning stops the monitor.
  taskbar.unpinPinnedOnlyApp(QString::fromLatin1(kRecycleBinId), button->hwnd());
  EXPECT_FALSE(taskbar.recycleBinMonitor()->active());
}

TEST_F(TaskbarPinTest, EmptyRecycleBinIsDisabledWhenThereIsNothingToEmpty) {
  TaskbarWidget& taskbar = buildPinned({{kRecycleBinId, "Recycle Bin"}});
  AppButton* button = taskbar.pinnedOnlyButton(QString::fromLatin1(kRecycleBinId));
  QMenu* menu = taskbar.showContextMenu(button->hwnd(), QPoint(10, 10));
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(titlesOf(menu), (QStringList{"Open", "Empty Recycle Bin", "Unpin from taskbar"}));
  EXPECT_FALSE(actionsOf(menu)[1]->isEnabled());
  menu->close();
}

TEST_F(TaskbarPinTest, ARunningRecycleBinWindowKeepsTheBinsIconWhenPinned) {
  TaskbarWidget& taskbar = buildPinned({{kRecycleBinId, "Recycle Bin"}});
  system_->add(kFirst, QStringLiteral("Recycle Bin"), QStringLiteral("explorer.exe")).uniqueId =
      QString::fromLatin1(kRecycleBinId);
  addTo(taskbar, kFirst);
  AppButton* running = taskbar.buttonFor(kFirst);
  ASSERT_NE(running, nullptr);
  EXPECT_EQ(qAlpha(running->iconLabel->pixmap().toImage().pixel(0, 0)), 0x40);
}

TEST(TaskbarPinIds, PseudoHandlesAreStableNegativeAndDistinct) {
  const WindowHandle a = pseudoHwndFor(QStringLiteral("a"));
  EXPECT_TRUE(isPseudoHwnd(a));
  EXPECT_EQ(a, pseudoHwndFor(QStringLiteral("a")));
  EXPECT_NE(a, pseudoHwndFor(QStringLiteral("b")));
  EXPECT_FALSE(isPseudoHwnd(static_cast<WindowHandle>(0x1234)));
}

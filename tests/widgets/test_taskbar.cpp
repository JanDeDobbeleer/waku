#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <map>
#include <nlohmann/json.hpp>

#include "core/bar/theme_class.h"
#include "core/config/loader.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/widget_context.h"
#include "platform/app_identity.h"
#include "platform/dwm_thumbnail.h"
#include "platform/window_system.h"
#include "platform/window_tracker.h"
#include "widgets/taskbar/taskbar_icons.h"
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
    WidgetContext context{
        "taskbar",       "yasb.taskbar.TaskbarWidget", options, {}, {}, eventBus_, globalState_,
        pollingWorkers_, taskbarOptions(options)};
    widget_.reset(new TaskbarWidget(context, std::move(deps)));
    widget_->setDevicePixelRatio(1.0);
    widget_->setInitialPopulation(false);
    return *widget_;
  }

  static QString classOf(const QWidget* widget) { return widget->property("class").toString(); }

  void addTo(TaskbarWidget& taskbar, WindowHandle hwnd) {
    taskbar.onWindowAdded(hwnd, system_->query(hwnd));
  }

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
  for (QAction* action : menu->actions()) titles << action->text();
  EXPECT_EQ(titles, (QStringList{"End task", "Close window", "Close all windows"}));

  menu->actions()[0]->trigger();
  menu->actions()[1]->trigger();
  menu->actions()[2]->trigger();
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

// --- tracker

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

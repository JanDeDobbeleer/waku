// TaskbarService, TaskbarModel and the Taskbar widget on a fake window system and fake shell
// operations: no real window is ever activated, minimised or closed, nothing is launched, and
// the pinned apps file is in a temporary folder (never %LOCALAPPDATA%\YASB).
#include <gtest/gtest.h>

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <map>
#include <memory>
#include <set>

#include "spike/services/taskbar_model.h"
#include "spike/services/taskbar_service.h"
#include "spike/tests/bar_fixtures.h"

namespace {

namespace plat = yasb::platform;
using tidy::services::TaskbarModel;
using tidy::services::TaskbarService;
using tidy::services::WindowIconProvider;

constexpr plat::WindowHandle kFirst = 101;
constexpr plat::WindowHandle kSecond = 102;
constexpr plat::WindowHandle kThird = 103;
constexpr plat::WindowHandle kFourth = 104;
constexpr plat::WindowHandle kFifth = 105;

// A window system that never touches the desktop: windows are rows in a map and every action is
// only recorded.
class FakeSystem final : public plat::WindowSystem {
 public:
  struct Row {
    plat::WindowInfo info;
    bool iconic = false;
    QString uniqueId = QStringLiteral("app");
  };

  Row& add(plat::WindowHandle hwnd, const QString& title, const QString& uniqueId = {},
           const QString& process = QStringLiteral("app.exe")) {
    Row& row = rows[hwnd];
    row.info.hwnd = hwnd;
    row.info.title = title;
    row.info.className = QStringLiteral("TestClass");
    row.info.processName = process;
    row.info.processPath = QStringLiteral("C:\\test\\") + process;
    row.info.pid = 1;
    row.info.canMinimize = true;
    row.info.monitor = barMonitor;
    if (!uniqueId.isEmpty()) row.uniqueId = uniqueId;
    return row;
  }

  bool isWindow(plat::WindowHandle hwnd) const override { return rows.count(hwnd) != 0; }
  bool isIconic(plat::WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() && found->second.iconic;
  }
  plat::WindowHandle foregroundWindow() const override { return foreground; }
  std::vector<plat::WindowHandle> enumerateWindows() const override {
    std::vector<plat::WindowHandle> result;
    for (const auto& [hwnd, row] : rows) result.push_back(hwnd);
    return result;
  }
  plat::WindowInfo query(plat::WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() ? found->second.info : plat::WindowInfo{};
  }
  bool isTaskbarWindow(const plat::WindowInfo& info) const override {
    return rows.count(info.hwnd) != 0 && !info.title.trimmed().isEmpty();
  }
  bool canMinimize(plat::WindowHandle hwnd) const override { return query(hwnd).canMinimize; }
  plat::WindowHandle monitorOfWindow(plat::WindowHandle) const override { return barMonitor; }
  QRect windowRect(plat::WindowHandle) const override { return QRect(0, 0, 800, 600); }
  std::pair<plat::WindowHandle, plat::WindowHandle> resolveBaseAndFocus(
      plat::WindowHandle hwnd) const override {
    return {hwnd, hwnd};
  }
  bool isOwnerRootActive(plat::WindowHandle base) const override { return foreground == base; }
  std::optional<QString> appUniqueId(const plat::WindowInfo& info) const override {
    const auto found = rows.find(info.hwnd);
    if (found == rows.end() || found->second.uniqueId.isEmpty()) return std::nullopt;
    return found->second.uniqueId;
  }
  plat::IconBitmap windowIcon(plat::WindowHandle, int size) const override {
    plat::IconBitmap bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, '\x80');
    return bitmap;
  }

  void restoreWindow(plat::WindowHandle hwnd) override {
    log << QStringLiteral("restore %1").arg(hwnd);
  }
  void minimizeWindow(plat::WindowHandle hwnd) override {
    log << QStringLiteral("minimize %1").arg(hwnd);
  }
  void showWindow(plat::WindowHandle hwnd) override { log << QStringLiteral("show %1").arg(hwnd); }
  void setForeground(plat::WindowHandle hwnd) override {
    log << QStringLiteral("foreground %1").arg(hwnd);
  }
  void closeApplication(plat::WindowHandle hwnd, bool force) override {
    log << QStringLiteral("%1 %2").arg(force ? "kill" : "close").arg(hwnd);
  }

  std::map<plat::WindowHandle, Row> rows;
  plat::WindowHandle foreground = 0;
  plat::WindowHandle barMonitor = 1;
  QStringList log;
};

// Shell operations that never touch the start menu or the registry.
class FakePins final : public plat::PinSupport {
 public:
  plat::PinMetadata resolveMetadata(const plat::WindowInfo& info, const QString&) const override {
    return {info.title, QStringLiteral("C:\\lnk\\") + info.title + QStringLiteral(".lnk")};
  }
  plat::IconBitmap pinnedIcon(const QString&, const plat::PinMetadata&, int size) const override {
    return filled(size, '\x30');
  }
  std::optional<QString> toastIconPath(const QString&) const override { return std::nullopt; }
  plat::IconBitmap defaultIcon() const override { return filled(16, '\x77'); }
  plat::IconBitmap recycleBinIcon(bool, int size) const override { return filled(size, '\x40'); }
  void launch(const QString& uniqueId, const plat::PinMetadata&, const QString&) override {
    launches << uniqueId;
  }
  plat::RecycleBinInfo recycleBinInfo() const override { return {}; }
  void emptyRecycleBin() override {}
  std::vector<plat::QuickAccessFolder> quickAccessFolders() const override { return {}; }

  static plat::IconBitmap filled(int size, char value) {
    plat::IconBitmap bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, value);
    return bitmap;
  }
  QStringList launches;
};

QString pinFileIn(const QTemporaryDir& dir) {
  return dir.filePath(QStringLiteral("taskbar_pinned.json"));
}

void writePins(const QString& path, const QStringList& ids) {
  QJsonObject apps;
  QJsonArray order;
  for (const QString& id : ids) {
    apps[id] = QJsonObject{{QStringLiteral("title"), id + QStringLiteral(" title")}};
    order.append(id);
  }
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(QJsonObject{{QStringLiteral("pinned_apps"), apps},
                                       {QStringLiteral("pinned_order"), order}})
                 .toJson());
}

QJsonObject readPins(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}

TaskbarService::Deps makeDeps(const std::shared_ptr<FakeSystem>& system,
                              const std::shared_ptr<FakePins>& pins, const QTemporaryDir& dir) {
  TaskbarService::Deps deps;
  deps.system = system;
  deps.pins = pins;
  deps.pinFile = pinFileIn(dir);
  deps.asyncIcons = false;
  return deps;
}

// ---- service and model without QML ----

struct Fixture {
  QTemporaryDir dir;
  std::shared_ptr<FakeSystem> system = std::make_shared<FakeSystem>();
  std::shared_ptr<FakePins> pins = std::make_shared<FakePins>();
  std::unique_ptr<TaskbarService> service;
  TaskbarModel model;

  // Builds the service (reading the windows already in `system`) and connects the model.
  void start() {
    service = std::make_unique<TaskbarService>(makeDeps(system, pins, dir));
    model.setService(service.get());
    ASSERT_TRUE(service->tracker()->start(1));
  }

  int rows() const { return model.rowCount(); }
  QVariant at(int row, int role) const { return model.data(model.index(row), role); }
  QString key(int row) const { return at(row, TaskbarModel::KeyRole).toString(); }
  QString title(int row) const { return at(row, TaskbarModel::TitleRole).toString(); }
  QStringList keys() const {
    QStringList result;
    for (int i = 0; i < rows(); ++i) result << key(i);
    return result;
  }
  void hook(std::uintptr_t what, plat::WindowHandle hwnd) {
    service->tracker()->handleShellHook(what, static_cast<std::intptr_t>(hwnd));
  }
};

}  // namespace

TEST(TaskbarModel, ShowsTheOpenWindowsInArrivalOrder) {
  Fixture f;
  f.system->add(kSecond, QStringLiteral("Second"), QStringLiteral("b"));
  f.system->add(kFirst, QStringLiteral("First"), QStringLiteral("a"));
  f.start();
  // The tracker lists by handle; the service keeps that order, then arrivals append.
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:101"), QStringLiteral("w:102")}));
  f.system->add(kThird, QStringLiteral("Third"), QStringLiteral("c"));
  f.hook(plat::shell::kWindowCreated, kThird);
  EXPECT_EQ(f.rows(), 3);
  EXPECT_EQ(f.key(2), QStringLiteral("w:103"));
  EXPECT_EQ(f.title(2), QStringLiteral("Third"));
}

TEST(TaskbarModel, RemovesAClosedWindowAndAnnouncesTheTitleChange) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("First"), QStringLiteral("a"));
  f.system->add(kSecond, QStringLiteral("Second"), QStringLiteral("b"));
  f.start();
  ASSERT_EQ(f.rows(), 2);

  QSignalSpy changed(&f.model, &QAbstractItemModel::dataChanged);
  f.system->rows[kFirst].info.title = QStringLiteral("Renamed");
  f.hook(plat::shell::kRedraw, kFirst);  // debounced by 50 ms
  ASSERT_TRUE(QTest::qWaitFor([&] { return f.title(0) == QStringLiteral("Renamed"); }, 2000));
  EXPECT_GE(changed.count(), 1);

  QSignalSpy removed(&f.model, &QAbstractItemModel::rowsRemoved);
  f.system->rows.erase(kFirst);
  f.hook(plat::shell::kWindowDestroyed, kFirst);
  EXPECT_EQ(removed.count(), 1);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:102")}));
}

TEST(TaskbarModel, GroupsTheWindowsOfOneAppWithACount) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("Doc 1"), QStringLiteral("editor"));
  f.system->add(kSecond, QStringLiteral("Doc 2"), QStringLiteral("editor"));
  f.system->add(kThird, QStringLiteral("Mail"), QStringLiteral("mail"));
  f.start();
  EXPECT_EQ(f.rows(), 3);  // grouping is off by default, as in Python

  f.model.setGrouping(true);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("g:editor"), QStringLiteral("g:mail")}));
  EXPECT_EQ(f.at(0, TaskbarModel::CountRole).toInt(), 2);
  EXPECT_EQ(f.at(1, TaskbarModel::CountRole).toInt(), 1);
  EXPECT_EQ(f.at(0, TaskbarModel::TooltipRole).toString(), QStringLiteral("Doc 1 (2 windows)"));

  // The group goes with its last window.
  f.system->rows.erase(kFirst);
  f.hook(plat::shell::kWindowDestroyed, kFirst);
  EXPECT_EQ(f.at(0, TaskbarModel::CountRole).toInt(), 1);
  f.system->rows.erase(kSecond);
  f.hook(plat::shell::kWindowDestroyed, kSecond);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("g:mail")}));
}

TEST(TaskbarModel, GroupsWindowsOfOneExeByPathWhenTheyHaveNoAumid) {
  // The real system falls back to "path:<exe>" when a window has no AUMID; the same key groups.
  Fixture f;
  f.system->add(kFirst, QStringLiteral("Edge 1"), QStringLiteral("aumid:Edge"));
  f.system->add(kSecond, QStringLiteral("Term 1"), QStringLiteral("path:C:\\t\\wt.exe"));
  f.system->add(kThird, QStringLiteral("Term 2"), QStringLiteral("path:C:\\t\\wt.exe"));
  f.start();
  f.model.setGrouping(true);
  EXPECT_EQ(f.rows(), 2);
  EXPECT_EQ(f.at(0, TaskbarModel::CountRole).toInt(), 1);
  EXPECT_EQ(f.at(1, TaskbarModel::CountRole).toInt(), 2);

  // A window without any id stays alone, even beside another one without an id.
  f.system->add(kFourth, QStringLiteral("Odd 1")).uniqueId.clear();
  f.system->add(kFifth, QStringLiteral("Odd 2")).uniqueId.clear();
  f.hook(plat::shell::kWindowCreated, kFourth);
  f.hook(plat::shell::kWindowCreated, kFifth);
  EXPECT_EQ(f.rows(), 4);
}

TEST(TaskbarModel, AGroupedRunningAppAbsorbsIntoItsPinnedButton) {
  Fixture f;
  writePins(pinFileIn(f.dir), {QStringLiteral("app")});
  f.system->add(kFirst, QStringLiteral("A1"), QStringLiteral("app"));
  f.system->add(kSecond, QStringLiteral("A2"), QStringLiteral("app"));
  f.start();
  f.model.setGrouping(true);
  ASSERT_EQ(f.rows(), 1);
  EXPECT_TRUE(f.at(0, TaskbarModel::PinnedRole).toBool());
  EXPECT_EQ(f.at(0, TaskbarModel::CountRole).toInt(), 2);
  f.system->rows.erase(kSecond);
  f.hook(plat::shell::kWindowDestroyed, kSecond);
  EXPECT_EQ(f.rows(), 1);
  EXPECT_EQ(f.at(0, TaskbarModel::CountRole).toInt(), 1);
}

TEST(TaskbarModel, FiltersWindowsLikeThePythonTaskbar) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("Plain"), QStringLiteral("a"));
  f.system->add(kSecond, QStringLiteral("Fixed size"), QStringLiteral("b")).info.canMinimize =
      false;
  f.system->add(kThird, QStringLiteral("Ignored"), QStringLiteral("c"), QStringLiteral("skip.exe"));
  f.start();
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:101"), QStringLiteral("w:103")}));

  f.model.setStrictFiltering(false);
  EXPECT_EQ(f.rows(), 3);
  f.model.setIgnoreProcesses({QStringLiteral("skip.exe")});
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:101"), QStringLiteral("w:102")}));
  f.model.setIgnoreTitles({QStringLiteral("Plain")});
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:102")}));
}

TEST(TaskbarModel, MonitorExclusiveKeepsOnlyTheWindowsOfTheBarsMonitor) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("Here"), QStringLiteral("a"));
  f.system->add(kSecond, QStringLiteral("There"), QStringLiteral("b")).info.monitor = 2;
  f.start();
  QWindow bar;
  bar.create();
  f.model.setWindow(&bar);
  QCoreApplication::processEvents();
  EXPECT_EQ(f.rows(), 2);  // every monitor by default

  f.model.setMonitorExclusive(true);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:101")}));
  f.model.setMonitorExclusive(false);
  EXPECT_EQ(f.rows(), 2);
}

TEST(TaskbarModel, ALeftClickFocusesMinimisesAndRestores) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  f.system->add(kSecond, QStringLiteral("Two"), QStringLiteral("b")).iconic = true;
  f.start();
  EXPECT_TRUE(f.at(1, TaskbarModel::MinimizedRole).toBool());

  f.model.activate(0);  // not in front: brought forward
  EXPECT_EQ(f.system->log,
            (QStringList{QStringLiteral("show 101"), QStringLiteral("foreground 101")}));

  f.system->log.clear();
  f.system->foreground = kFirst;
  f.hook(plat::shell::kWindowActivated, kFirst);
  EXPECT_TRUE(f.at(0, TaskbarModel::ActiveRole).toBool());
  f.model.activate(0);  // in front: minimised
  EXPECT_EQ(f.system->log, (QStringList{QStringLiteral("minimize 101")}));

  f.system->log.clear();
  f.model.activate(1);  // minimised: restored
  EXPECT_EQ(f.system->log,
            (QStringList{QStringLiteral("restore 102"), QStringLiteral("foreground 102")}));
}

TEST(TaskbarModel, AClickOnAGroupCyclesThroughItsWindows) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("Doc 1"), QStringLiteral("editor"));
  f.system->add(kSecond, QStringLiteral("Doc 2"), QStringLiteral("editor"));
  f.model.setGrouping(true);
  f.start();
  ASSERT_EQ(f.rows(), 1);

  f.model.activate(0);  // none in front: the first
  EXPECT_EQ(f.system->log.last(), QStringLiteral("foreground 101"));
  f.hook(plat::shell::kWindowActivated, kFirst);
  f.system->log.clear();
  f.model.activate(0);  // the next one
  EXPECT_EQ(f.system->log,
            (QStringList{QStringLiteral("show 102"), QStringLiteral("foreground 102")}));
}

TEST(TaskbarModel, MiddleClickClosesAndTheMenuClosesOneOrAll) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("Doc 1"), QStringLiteral("editor"));
  f.system->add(kSecond, QStringLiteral("Doc 2"), QStringLiteral("editor"));
  f.model.setGrouping(true);
  f.start();
  f.model.middleClick(0);
  EXPECT_EQ(f.system->log, (QStringList{QStringLiteral("close 101")}));
  f.system->log.clear();
  f.model.closeAll(0);
  EXPECT_EQ(f.system->log, (QStringList{QStringLiteral("close 101"), QStringLiteral("close 102")}));
  f.system->log.clear();
  f.model.closeWindow(7);  // no such row
  EXPECT_TRUE(f.system->log.isEmpty());
}

TEST(TaskbarModel, PinsComeFirstAndSurviveInTheSharedFile) {
  Fixture f;
  writePins(pinFileIn(f.dir), {QStringLiteral("notrunning")});
  f.system->add(kFirst, QStringLiteral("Loose"), QStringLiteral("loose"));
  f.system->add(kSecond, QStringLiteral("Mail"), QStringLiteral("mail"));
  f.start();
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("p:notrunning"), QStringLiteral("w:101"),
                                   QStringLiteral("w:102")}));
  EXPECT_FALSE(f.at(0, TaskbarModel::RunningRole).toBool());
  EXPECT_TRUE(f.at(0, TaskbarModel::PinnedRole).toBool());
  EXPECT_EQ(f.title(0), QStringLiteral("notrunning title"));

  // Pinning a running app moves it behind the other pins and writes the shared file.
  f.model.togglePin(2);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("p:notrunning"), QStringLiteral("w:102"),
                                   QStringLiteral("w:101")}));
  EXPECT_TRUE(f.at(1, TaskbarModel::PinnedRole).toBool());
  const QJsonObject saved = readPins(pinFileIn(f.dir));
  EXPECT_EQ(saved.value(QStringLiteral("pinned_order")).toArray(),
            (QJsonArray{QStringLiteral("notrunning"), QStringLiteral("mail")}));
  const QJsonObject mail = saved.value(QStringLiteral("pinned_apps"))
                               .toObject()
                               .value(QStringLiteral("mail"))
                               .toObject();
  EXPECT_EQ(mail.value(QStringLiteral("title")).toString(), QStringLiteral("Mail"));
  EXPECT_EQ(mail.value(QStringLiteral("shortcut_path")).toString(),
            QStringLiteral("C:\\lnk\\Mail.lnk"));

  // Unpinning a running app keeps its button; unpinning one that does not run removes it.
  f.model.togglePin(1);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("p:notrunning"), QStringLiteral("w:101"),
                                   QStringLiteral("w:102")}));
  f.model.togglePin(0);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:101"), QStringLiteral("w:102")}));
  EXPECT_TRUE(readPins(pinFileIn(f.dir)).value(QStringLiteral("pinned_order")).toArray().isEmpty());
}

TEST(TaskbarModel, APinnedAppThatIsNotRunningLaunchesOnClick) {
  Fixture f;
  writePins(pinFileIn(f.dir), {QStringLiteral("x")});
  f.start();
  ASSERT_EQ(f.rows(), 1);
  f.model.activate(0);
  f.model.middleClick(0);
  f.model.launch(0);
  EXPECT_EQ(f.pins->launches,
            (QStringList{QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("x")}));
  EXPECT_TRUE(f.system->log.isEmpty());
}

TEST(TaskbarModel, APinnedAppTakesTheSlotOfItsWindowWhenItStarts) {
  Fixture f;
  writePins(pinFileIn(f.dir), {QStringLiteral("app")});
  f.start();
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("p:app")}));
  f.system->add(kFirst, QStringLiteral("Started"), QStringLiteral("app"));
  f.hook(plat::shell::kWindowCreated, kFirst);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("w:101")}));
  EXPECT_TRUE(f.at(0, TaskbarModel::PinnedRole).toBool());
  EXPECT_TRUE(f.at(0, TaskbarModel::RunningRole).toBool());
}

TEST(TaskbarModel, TheIconProviderServesTheWindowAndThePinnedIcon) {
  Fixture f;
  writePins(pinFileIn(f.dir), {QStringLiteral("x")});
  f.system->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  f.start();
  WindowIconProvider provider(f.service.get());

  const QString url = f.at(1, TaskbarModel::IconUrlRole).toString();
  EXPECT_TRUE(url.startsWith(QStringLiteral("image://windowicon/101/")));
  QSize size;
  const QImage window =
      provider.requestImage(url.mid(QStringLiteral("image://windowicon/").size()), &size, QSize());
  EXPECT_FALSE(window.isNull());
  EXPECT_EQ(size, QSize(TaskbarService::kIconSize, TaskbarService::kIconSize));

  const QString pinned = f.at(0, TaskbarModel::IconUrlRole).toString();
  EXPECT_TRUE(pinned.startsWith(QStringLiteral("image://windowicon/")));
  EXPECT_FALSE(
      provider
          .requestImage(pinned.mid(QStringLiteral("image://windowicon/").size()), &size, QSize())
          .isNull());
  EXPECT_TRUE(provider.requestImage(QStringLiteral("nonsense"), &size, QSize()).isNull());
}

TEST(TaskbarModel, AFlashingWindowIsMarkedUntilItIsFocused) {
  Fixture f;
  f.system->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  f.start();
  f.hook(plat::shell::kFlash, kFirst);
  EXPECT_TRUE(f.at(0, TaskbarModel::FlashingRole).toBool());
  f.hook(plat::shell::kWindowActivated, kFirst);
  EXPECT_FALSE(f.at(0, TaskbarModel::FlashingRole).toBool());
  EXPECT_TRUE(f.at(0, TaskbarModel::ActiveRole).toBool());
}

TEST(TaskbarModel, WithoutATrackerThePinnedAppsStillShow) {
  Fixture f;
  writePins(pinFileIn(f.dir), {QStringLiteral("x")});
  TaskbarService::Deps deps = makeDeps(f.system, f.pins, f.dir);
  deps.trackerAvailable = false;
  deps.unavailableReason = QStringLiteral("taskbar unavailable: test");
  TaskbarService service(std::move(deps));
  EXPECT_FALSE(service.available());
  EXPECT_EQ(service.unavailableReason(), QStringLiteral("taskbar unavailable: test"));
  f.model.setService(&service);
  EXPECT_EQ(f.keys(), (QStringList{QStringLiteral("p:x")}));
}

// ---- the widget in a real offscreen bar ----

namespace {

QPoint centerIn(QQuickWindow* window, const QString& name) {
  QQuickItem* item = findItem(window, name);
  return item ? item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint()
              : QPoint(-1, -1);
}

QQuickWindow* visibleWindow(const QString& name) {
  for (QWindow* window : QGuiApplication::allWindows()) {
    if (window->objectName() == name && window->isVisible()) {
      return qobject_cast<QQuickWindow*>(window);
    }
  }
  return nullptr;
}

class TaskbarWidgetTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    TaskbarService::setFactoryForTests([this]() -> std::unique_ptr<TaskbarService> {
      return std::make_unique<TaskbarService>(makeDeps(system_, pins_, dir_));
    });
  }
  void TearDown() override { TaskbarService::setFactoryForTests({}); }

  bool start(Harness& h, const QString& props = QString()) {
    h.start(QStringLiteral(R"(
import QtQuick
import tidy
import tidy.widgets
Config {
    Bar {
        layout: BarLayout {
            start: [ Taskbar { objectName: "taskbar"; %1 } ]
        }
    }
}
)")
                .arg(props));
    if (!h.host->reload()) {
      qWarning() << "bar error:" << h.host->error();
      return false;
    }
    return h.window() && QTest::qWaitForWindowExposed(h.window());
  }
  bool waitFor(Harness& h, const QString& name) {
    return QTest::qWaitFor([&] { return findItem(h.window(), name) != nullptr; }, 3000);
  }

  QTemporaryDir dir_;
  std::shared_ptr<FakeSystem> system_ = std::make_shared<FakeSystem>();
  std::shared_ptr<FakePins> pins_ = std::make_shared<FakePins>();
};

}  // namespace

TEST_F(TaskbarWidgetTest, TheWindowsOfTheDesktopAppearAsButtons) {
  system_->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  system_->add(kSecond, QStringLiteral("Two"), QStringLiteral("b"));
  Harness h;
  ASSERT_TRUE(start(h));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-w:101")));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-w:102")));
  QQuickItem* first = findItem(h.window(), QStringLiteral("taskbarButton-w:101"));
  EXPECT_TRUE(first->isVisible());
  EXPECT_EQ(first->width(), 24);  // iconSize 16 + 8
  EXPECT_LT(positionIn(h.window(), QStringLiteral("taskbarButton-w:101")).x(),
            positionIn(h.window(), QStringLiteral("taskbarButton-w:102")).x());
  EXPECT_TRUE(findItem(h.window(), QStringLiteral("taskbar"))->isVisible());
}

TEST_F(TaskbarWidgetTest, HideEmptyHidesTheWidgetUntilAWindowAppears) {
  Harness h;
  ASSERT_TRUE(start(h, QStringLiteral("hideEmpty: true")));
  QTest::qWait(100);
  EXPECT_FALSE(findItem(h.window(), QStringLiteral("taskbar"))->isVisible());
}

TEST_F(TaskbarWidgetTest, ALeftClickFocusesTheRightWindow) {
  system_->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  system_->add(kSecond, QStringLiteral("Two"), QStringLiteral("b"));
  Harness h;
  ASSERT_TRUE(start(h));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-w:102")));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("taskbarButton-w:102")));
  EXPECT_EQ(system_->log,
            (QStringList{QStringLiteral("show 102"), QStringLiteral("foreground 102")}));
}

TEST_F(TaskbarWidgetTest, AMiddleClickClosesTheWindow) {
  system_->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  Harness h;
  ASSERT_TRUE(start(h));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-w:101")));
  QTest::mouseClick(h.window(), Qt::MiddleButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("taskbarButton-w:101")));
  EXPECT_EQ(system_->log, (QStringList{QStringLiteral("close 101")}));
}

TEST_F(TaskbarWidgetTest, GroupingShowsOneButtonWithACount) {
  system_->add(kFirst, QStringLiteral("Doc 1"), QStringLiteral("editor"));
  system_->add(kSecond, QStringLiteral("Doc 2"), QStringLiteral("editor"));
  Harness h;
  ASSERT_TRUE(start(h, QStringLiteral("grouping: true")));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-g:editor")));
  EXPECT_EQ(findItem(h.window(), QStringLiteral("taskbarButton-w:101")), nullptr);
  QQuickItem* count = findItem(h.window(), QStringLiteral("taskbarCount"));
  ASSERT_NE(count, nullptr);
  EXPECT_TRUE(count->isVisible());
  EXPECT_EQ(count->property("text").toString(), QStringLiteral("2"));
}

TEST_F(TaskbarWidgetTest, TheMenuPinsTheAppIntoTheSharedFile) {
  system_->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  Harness h;
  ASSERT_TRUE(start(h));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-w:101")));
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("taskbarButton-w:101")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return visibleWindow(QStringLiteral("taskbarMenu")); }, 2000));
  QQuickWindow* menu = visibleWindow(QStringLiteral("taskbarMenu"));
  ASSERT_TRUE(QTest::qWaitForWindowExposed(menu));
  EXPECT_TRUE(system_->log.isEmpty());  // a right click does not touch the window
  EXPECT_EQ(findItem(menu, QStringLiteral("taskbarMenuPin"))->property("text").toString(),
            QStringLiteral("Pin to taskbar"));
  EXPECT_FALSE(findItem(menu, QStringLiteral("taskbarMenuCloseAll"))->isVisible());  // one window

  QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    centerIn(menu, QStringLiteral("taskbarMenuPin")));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] {
        return readPins(pinFileIn(dir_)).value(QStringLiteral("pinned_order")).toArray() ==
               QJsonArray{QStringLiteral("a")};
      },
      2000));
  EXPECT_FALSE(menu->isVisible());
  EXPECT_TRUE(system_->log.isEmpty());
}

TEST_F(TaskbarWidgetTest, TheMenuClosesTheWindow) {
  system_->add(kFirst, QStringLiteral("One"), QStringLiteral("a"));
  Harness h;
  ASSERT_TRUE(start(h));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-w:101")));
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("taskbarButton-w:101")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return visibleWindow(QStringLiteral("taskbarMenu")); }, 2000));
  QQuickWindow* menu = visibleWindow(QStringLiteral("taskbarMenu"));
  ASSERT_TRUE(QTest::qWaitForWindowExposed(menu));
  QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    centerIn(menu, QStringLiteral("taskbarMenuClose")));
  EXPECT_EQ(system_->log, (QStringList{QStringLiteral("close 101")}));
}

TEST_F(TaskbarWidgetTest, APinnedAppThatIsNotRunningShowsAndLaunches) {
  writePins(pinFileIn(dir_), {QStringLiteral("x")});
  Harness h;
  ASSERT_TRUE(start(h));
  ASSERT_TRUE(waitFor(h, QStringLiteral("taskbarButton-p:x")));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                    centerIn(h.window(), QStringLiteral("taskbarButton-p:x")));
  EXPECT_EQ(pins_->launches, (QStringList{QStringLiteral("x")}));
  EXPECT_TRUE(system_->log.isEmpty());
}

// The core types of the `Quickshell` module that exist only for QML written for Quickshell:
// ScriptModel, SystemClock, PersistentProperties and IpcHandler (with `wabarc call`).
#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include "host/qml/control_handler.h"
#include "host/qml/ipc_handler.h"
#include "host/qml/script_model.h"
#include "host/qml/system_clock.h"
#include "host/testing/shell_fixtures.h"

using namespace wabar::host;

namespace {

std::unique_ptr<QObject> create(QQmlEngine& engine, const QByteArray& source) {
  QQmlComponent component(&engine);
  component.setData(source, QUrl(QStringLiteral("file:///core.qml")));
  EXPECT_FALSE(component.isError()) << qPrintable(component.errorString());
  return std::unique_ptr<QObject>(component.create());
}

QStringList column(const QAbstractItemModel& model) {
  QStringList result;
  for (int i = 0; i < model.rowCount(); ++i) {
    result << model.data(model.index(i, 0), Qt::UserRole).toString();
  }
  return result;
}

}  // namespace

// ---- ScriptModel ----

TEST(QuickshellScriptModel, UpdatesWithoutResettingAndKeepsRows) {
  ScriptModel model;
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
  QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
  model.setValues({"a", "b", "c"});
  EXPECT_EQ(column(model), (QStringList{"a", "b", "c"}));
  inserted.clear();

  model.setValues({"b", "x", "a"});  // c gone, x new, a moved after b
  EXPECT_EQ(column(model), (QStringList{"b", "x", "a"}));
  EXPECT_EQ(reset.size(), 0);
  EXPECT_EQ(removed.size(), 1);
  EXPECT_EQ(inserted.size(), 1);
  EXPECT_GE(moved.size(), 1);

  model.setValues({});
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(QuickshellScriptModel, ObjectPropMatchesObjectsAndReplacesTheValue) {
  ScriptModel model;
  model.setObjectProp(QStringLiteral("id"));
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  model.setValues({QVariantMap{{"id", 1}, {"n", "one"}}, QVariantMap{{"id", 2}, {"n", "two"}}});
  inserted.clear();
  model.setValues({QVariantMap{{"id", 1}, {"n", "uno"}}, QVariantMap{{"id", 2}, {"n", "two"}}});
  EXPECT_EQ(inserted.size(), 0);  // same ids: the rows stay
  EXPECT_EQ(changed.size(), 1);   // only row 1 differs
  EXPECT_EQ(model.data(model.index(0, 0), Qt::UserRole).toMap().value("n").toString(), "uno");
}

TEST(QuickshellScriptModel, QmlListViewKeepsDelegates) {
  QQmlEngine engine;
  auto root = create(engine, R"(
import QtQuick
import Quickshell
Item {
    id: root
    property var items: ["a", "b", "c"]
    property int created: 0
    property alias model: sm
    ScriptModel { id: sm; values: root.items }
    ListView {
        id: lv
        objectName: "lv"
        width: 100; height: 300
        model: sm
        delegate: Item { required property string modelData; height: 10; Component.onCompleted: root.created++ }
    }
}
)");
  ASSERT_NE(root, nullptr);
  QQuickWindow window;
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show();
  QTest::qWait(50);
  EXPECT_EQ(root->property("created").toInt(), 3);
  root->setProperty("items", QVariantList{"a", "c", "d"});
  QTest::qWait(50);
  EXPECT_EQ(root->property("created").toInt(), 4);  // only "d" is new
}

// ---- SystemClock ----

TEST(QuickshellSystemClock, BoundariesAndTruncation) {
  const QDateTime t(QDate(2026, 10, 30), QTime(13, 45, 20, 250));
  EXPECT_EQ(SystemClock::truncated(t, SystemClock::Seconds).time(), QTime(13, 45, 20));
  EXPECT_EQ(SystemClock::truncated(t, SystemClock::Minutes).time(), QTime(13, 45, 0));
  EXPECT_EQ(SystemClock::truncated(t, SystemClock::Hours).time(), QTime(13, 0, 0));
  EXPECT_EQ(SystemClock::msToNextBoundary(t, SystemClock::Seconds), 750);
  EXPECT_EQ(SystemClock::msToNextBoundary(t, SystemClock::Minutes), 39750);
  EXPECT_EQ(SystemClock::msToNextBoundary(t, SystemClock::Hours), 879750);
}

TEST(QuickshellSystemClock, ReadsTheSourceAndFollowsTick) {
  QDateTime now(QDate(2026, 10, 30), QTime(9, 5, 59, 900));
  SystemClock::setTimeSource([&now] { return now; });
  {
    QQmlEngine engine;
    auto root = create(engine, R"(
import QtQuick
import Quickshell
SystemClock { precision: SystemClock.Minutes }
)");
    ASSERT_NE(root, nullptr);
    auto* clock = qobject_cast<SystemClock*>(root.get());
    ASSERT_NE(clock, nullptr);
    EXPECT_EQ(clock->hours(), 9);
    EXPECT_EQ(clock->minutes(), 5);
    EXPECT_EQ(clock->seconds(), 0);
    QSignalSpy minutes(clock, &SystemClock::minutesChanged);
    now = now.addMSecs(200);  // 9:06:00.100
    clock->tick();
    EXPECT_EQ(clock->minutes(), 6);
    EXPECT_EQ(minutes.size(), 1);
    clock->setEnabled(false);
    now = now.addSecs(120);
    clock->tick();
    EXPECT_EQ(clock->minutes(), 6);  // paused
    clock->setPrecision(SystemClock::Hours);
    clock->setEnabled(true);
    EXPECT_EQ(clock->minutes(), 0);
    EXPECT_EQ(clock->hours(), 9);
  }
  SystemClock::setTimeSource({});
}

// ---- PersistentProperties ----

TEST(QuickshellPersistentProperties, ValuesSurviveAReload) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import Quickshell

ShellRoot {
    PersistentProperties {
        objectName: "pp"
        reloadableId: "state"
        property int n: 1
        property string s: "x"
        property bool restored: false
        onReloaded: restored = true
    }
    PersistentProperties { objectName: "other"; reloadableId: "other"; property int n: 5 }
    Panel { content: PanelLayout {} }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  QObject* first = h.host->shell()->findChild<QObject*>(QStringLiteral("pp"));
  ASSERT_NE(first, nullptr);
  EXPECT_FALSE(first->property("restored").toBool());
  first->setProperty("n", 42);
  first->setProperty("s", "kept");
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  QObject* second = h.host->shell()->findChild<QObject*>(QStringLiteral("pp"));
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->property("n").toInt(), 42);
  EXPECT_EQ(second->property("s").toString(), "kept");
  EXPECT_TRUE(second->property("restored").toBool());
  QObject* other = h.host->shell()->findChild<QObject*>(QStringLiteral("other"));
  EXPECT_EQ(other->property("n").toInt(), 5);
}

TEST(QuickshellPersistentProperties, DestroyedObjectLeavesItsValuesForTheNext) {
  QQmlEngine engine;
  const QByteArray source = R"(
import QtQuick
import Quickshell
PersistentProperties { reloadableId: "lazy"; property int n: 1 }
)";
  auto first = create(engine, source);
  ASSERT_NE(first, nullptr);
  first->setProperty("n", 9);
  first.reset();
  auto second = create(engine, source);
  EXPECT_EQ(second->property("n").toInt(), 9);
}

// ---- IpcHandler ----

namespace {
const char* kHandlerQml = R"(
import QtQuick
import Quickshell
Item {
    property string last
    IpcHandler {
        target: "t"
        function echo(text: string): string { return text }
        function add(a: int, b: int): int { return a + b }
        function half(x: real): real { return x / 2 }
        function flag(v: bool): bool { return !v }
        function tint(c: color): color { return c }
        function note(text: string): void { last = text }
        function info(): var { return { "a": 1, "list": [1, 2] } }
        function two(a: string, b: string): string { return a + "+" + b }
    }
    IpcHandler { target: "off"; enabled: false; function f(): string { return "no" } }
}
)";
}

TEST(QuickshellIpcHandler, CallsConvertArgumentsAndReturnOneLine) {
  QQmlEngine engine;
  auto root = create(engine, kHandlerQml);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(IpcHandler::callByName({"t", "echo", "hello world"}), "hello world");
  EXPECT_EQ(IpcHandler::callByName({"t", "add", "2", "40"}), "42");
  EXPECT_EQ(IpcHandler::callByName({"t", "half", "5"}), "2.5");
  EXPECT_EQ(IpcHandler::callByName({"t", "flag", "true"}), "false");
  EXPECT_EQ(IpcHandler::callByName({"t", "flag", "0"}), "true");
  EXPECT_EQ(IpcHandler::callByName({"t", "tint", "ff8800"}), "#ffff8800");
  EXPECT_EQ(IpcHandler::callByName({"t", "echo", "a\nb"}), "\"a\\nb\"");
  EXPECT_EQ(IpcHandler::callByName({"t", "note", "seen"}), "ok");
  EXPECT_EQ(root->property("last").toString(), "seen");
  EXPECT_EQ(IpcHandler::callByName({"t", "info"}), "{\"a\":1,\"list\":[1,2]}");
  EXPECT_EQ(IpcHandler::callByName({"t", "two", "x", "y"}), "x+y");
}

TEST(QuickshellIpcHandler, ErrorsAreReported) {
  QQmlEngine engine;
  auto root = create(engine, kHandlerQml);
  ASSERT_NE(root, nullptr);
  EXPECT_TRUE(IpcHandler::callByName({"nope", "x"}).startsWith("error: no handler"));
  EXPECT_TRUE(IpcHandler::callByName({"off", "f"}).startsWith("error: no handler"));
  EXPECT_TRUE(IpcHandler::callByName({"t", "missing"}).startsWith("error: 't' has no function"));
  EXPECT_TRUE(IpcHandler::callByName({"t", "add", "1"}).startsWith("error: add takes 2"));
  EXPECT_TRUE(IpcHandler::callByName({"t", "add", "1", "x"}).contains("cannot read 'x' as int"));
  EXPECT_TRUE(IpcHandler::callByName({"t", "tint", "zzz"}).contains("as color"));
  EXPECT_TRUE(IpcHandler::callByName({"t"}).startsWith("error: usage"));
  const QString list = IpcHandler::callByName({});
  EXPECT_TRUE(list.contains("t: ")) << qPrintable(list);
  EXPECT_TRUE(list.contains("add(int a, int b): int")) << qPrintable(list);
  EXPECT_FALSE(list.contains("off:"));
}

TEST(QuickshellIpcHandler, HandlerGoneMeansNoTarget) {
  QQmlEngine engine;
  {
    auto root = create(engine, kHandlerQml);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(IpcHandler::callByName({"t", "echo", "x"}), "x");
  }
  EXPECT_TRUE(IpcHandler::callByName({"t", "echo", "x"}).startsWith("error: no handler"));
}

TEST(QuickshellIpcHandler, ControlHandlerCallVerb) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import Quickshell

ShellRoot {
    IpcHandler { target: "bar"; function greet(name: string): string { return "hi " + name } }
    Panel { content: PanelLayout {} }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  const wabar::control::ControlHandler handler = makeControlHandler(*h.host, false);
  EXPECT_EQ(handler(QStringLiteral("call bar greet \"big bar\"")), "hi big bar");
  EXPECT_TRUE(handler(QStringLiteral("call bar nope")).startsWith("error:"));
  EXPECT_TRUE(handler(QStringLiteral("call")).contains("greet(string name): string"));
  EXPECT_TRUE(handler(QStringLiteral("call \"oops")).startsWith("error: unterminated"));
}

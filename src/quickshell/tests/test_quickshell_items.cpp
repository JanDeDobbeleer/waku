// `Singleton` and `TransformWatcher` of the Quickshell module: the root type of a `pragma
// Singleton` file, and the item transform watcher.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <memory>

namespace {

void writeFile(const QString& path, const QByteArray& text) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(text);
}

std::unique_ptr<QObject> create(QQmlEngine* engine, const QByteArray& source,
                                const QUrl& url = QUrl(QStringLiteral("file:///items.qml"))) {
  QQmlComponent component(engine);
  component.setData(source, url);
  EXPECT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  EXPECT_NE(root, nullptr) << qPrintable(component.errorString());
  return root;
}

}  // namespace

TEST(QuickshellSingleton, APragmaSingletonRootIsReadFromAnotherFile) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  writeFile(dir.filePath(QStringLiteral("qmldir")), "singleton Store 1.0 Store.qml\n");
  writeFile(dir.filePath(QStringLiteral("Store.qml")), R"(
pragma Singleton
import QtQml
import Quickshell

Singleton {
    id: root
    property int x: 3
    readonly property int doubled: x * 2
    function bump() { x = x + 1; return x }
}
)");
  QQmlEngine engine;
  auto root = create(&engine, R"(
import QtQml
import "."

QtObject {
    property int x: Store.x
    property int doubled: Store.doubled
    property var store: Store
    property int after: Store.x
}
)",
                     QUrl::fromLocalFile(dir.filePath(QStringLiteral("main.qml"))));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->property("x").toInt(), 3);
  EXPECT_EQ(root->property("doubled").toInt(), 6);
  auto* store = root->property("store").value<QObject*>();
  ASSERT_NE(store, nullptr);
  QVariant bumped;
  ASSERT_TRUE(QMetaObject::invokeMethod(store, "bump", Q_RETURN_ARG(QVariant, bumped)));
  EXPECT_EQ(bumped.toInt(), 4);
  EXPECT_EQ(root->property("after").toInt(), 4);
}

TEST(QuickshellSingleton, ItAcceptsNonVisualChildren) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  writeFile(dir.filePath(QStringLiteral("qmldir")), "singleton Jobs 1.0 Jobs.qml\n");
  writeFile(dir.filePath(QStringLiteral("Jobs.qml")), R"(
pragma Singleton
import QtQml
import Quickshell
import Quickshell.Io

Singleton {
    id: root
    property int ticks: 0
    property alias inner: marker.objectName
    Timer { id: timer; interval: 5; running: true; repeat: false; onTriggered: root.ticks = 1 }
    QtObject { id: marker; objectName: "marker" }
    Process { id: proc; command: ["x"]; running: false }
    Scope { property int nested: 7 }
    readonly property int childCount: data.length
}
)");
  QQmlEngine engine;
  auto root = create(&engine, R"(
import QtQml
import "."

QtObject {
    property string inner: Jobs.inner
    property int childCount: Jobs.childCount
}
)",
                     QUrl::fromLocalFile(dir.filePath(QStringLiteral("main.qml"))));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->property("inner").toString(), QStringLiteral("marker"));
  EXPECT_EQ(root->property("childCount").toInt(), 4);
}

TEST(QuickshellSingleton, ItIsAvailableUnderTheWakuName) {
  QQmlEngine engine;
  auto root = create(&engine, R"(
import QtQml
import waku

Singleton { property int x: 5 }
)");
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->property("x").toInt(), 5);
}

namespace {

constexpr const char* kTree = R"(
import QtQuick
import Quickshell

Item {
    id: root
    width: 300; height: 300
    property int count: 0
    property alias watcher: w

    Item {
        id: left
        objectName: "left"
        x: 10; y: 10
        Item {
            id: mid
            objectName: "mid"
            x: 5; y: 5
            Item { id: itemA; objectName: "itemA"; x: 1; y: 1; width: 20; height: 20 }
        }
    }
    Item {
        id: right
        objectName: "right"
        x: 100
        Item { id: itemB; objectName: "itemB"; width: 10; height: 10 }
    }
    TransformWatcher { id: w; a: itemA; b: itemB }
    Connections { target: w; function onTransformChanged() { root.count += 1 } }
    function reset() { count = 0 }
    function reparentB() { itemB.parent = mid }
}
)";

struct Tree {
  QQmlEngine engine;
  std::unique_ptr<QObject> root;
  Tree() {
    root = create(&engine, kTree);
    EXPECT_TRUE(QMetaObject::invokeMethod(root.get(), "reset"));
  }
  QObject* item(const char* name) const {
    return root->findChild<QObject*>(QString::fromLatin1(name));
  }
  int count() const { return root->property("count").toInt(); }
};

}  // namespace

TEST(QuickshellTransformWatcher, MovingAnItemOnThePathFires) {
  Tree t;
  ASSERT_NE(t.root, nullptr);
  t.item("left")->setProperty("x", 40);  // an ancestor of a
  EXPECT_EQ(t.count(), 1);
  t.item("right")->setProperty("y", 9);  // an ancestor of b
  EXPECT_EQ(t.count(), 2);
  t.item("itemA")->setProperty("x", 3);  // a itself
  EXPECT_EQ(t.count(), 3);
}

TEST(QuickshellTransformWatcher, ResizingAnItemOnThePathFires) {
  Tree t;
  ASSERT_NE(t.root, nullptr);
  t.item("itemB")->setProperty("width", 30);
  EXPECT_EQ(t.count(), 1);
  t.item("mid")->setProperty("height", 50);
  EXPECT_EQ(t.count(), 2);
}

TEST(QuickshellTransformWatcher, AnItemOffThePathDoesNotFire) {
  Tree t;
  ASSERT_NE(t.root, nullptr);
  t.root->setProperty("x", 20);  // the common ancestor: the transform between a and b is the same
  EXPECT_EQ(t.count(), 0);
}

TEST(QuickshellTransformWatcher, ReparentingRebuildsThePath) {
  Tree t;
  ASSERT_NE(t.root, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(t.root.get(), "reparentB"));
  EXPECT_GE(t.count(), 1);  // the parent of b changed
  const int afterReparent = t.count();

  t.item("right")->setProperty("x", 0);  // no longer an ancestor of b
  EXPECT_EQ(t.count(), afterReparent);
  t.item("left")->setProperty("x", 70);  // above the new common ancestor (mid): off the path
  EXPECT_EQ(t.count(), afterReparent);
  t.item("itemA")->setProperty("y", 4);  // a is a sibling of b under mid now
  EXPECT_EQ(t.count(), afterReparent + 1);
  t.item("mid")->setProperty("x", 0);  // the common ancestor
  EXPECT_EQ(t.count(), afterReparent + 1);
}

TEST(QuickshellTransformWatcher, ChangingTheItemsFiresAndRewatches) {
  Tree t;
  ASSERT_NE(t.root, nullptr);
  auto* watcher = t.root->property("watcher").value<QObject*>();
  ASSERT_NE(watcher, nullptr);
  ASSERT_TRUE(
      watcher->setProperty("b", QVariant::fromValue(qobject_cast<QQuickItem*>(t.item("left")))));
  EXPECT_GE(t.count(), 1);
  const int before = t.count();
  t.item("right")->setProperty("x", 1);  // not on the path from itemA to left any more
  EXPECT_EQ(t.count(), before);
  t.item("itemA")->setProperty("x", 6);
  EXPECT_EQ(t.count(), before + 1);
}

TEST(QuickshellSingleton, ASingletonMayDeclareItsOwnDataAndReloadableId) {
  // Noctalia's Settings and ShellState declare `readonly property alias data`, and
  // BrightnessService sets `reloadableId`; both must load on the engine's Singleton.
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  writeFile(dir.filePath(QStringLiteral("qmldir")), "singleton Store 1.0 Store.qml\n");
  writeFile(dir.filePath(QStringLiteral("Store.qml")), R"(
pragma Singleton
import QtQml
import Quickshell

Singleton {
    id: root
    reloadableId: "store"
    readonly property alias data: inner
    QtObject { id: inner; property int v: 7 }
}
)");
  QQmlEngine engine;
  auto root = create(&engine, R"(
import QtQml
import "."

QtObject {
    property int v: Store.data.v
    property string id: Store.reloadableId
}
)",
                     QUrl::fromLocalFile(dir.filePath(QStringLiteral("main.qml"))));
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->property("v").toInt(), 7);
  EXPECT_EQ(root->property("id").toString(), QStringLiteral("store"));
}

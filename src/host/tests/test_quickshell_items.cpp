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

TEST(QuickshellSingleton, ItIsAvailableUnderTheWabarName) {
  QQmlEngine engine;
  auto root = create(&engine, R"(
import QtQml
import wabar

Singleton { property int x: 5 }
)");
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->property("x").toInt(), 5);
}

// Qt QML modules that shells written for Quickshell import although no built-in wabar file does.
// They reach the deployed folder through cmake/deploy_imports (windeployqt --qmldir); this test
// checks that the engine can load each one.
#include <gtest/gtest.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>

namespace {

void expectLoads(const QByteArray& source) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(source, QUrl(QStringLiteral("file:///imports.qml")));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  EXPECT_NE(root, nullptr) << qPrintable(component.errorString());
}

}  // namespace

TEST(QtImports, LayoutsLoad) {
  expectLoads(R"(
import QtQuick
import QtQuick.Layouts
Item {
    width: 100; height: 40
    RowLayout {
        anchors.fill: parent
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 10; color: "red" }
        Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 10; color: "blue" }
    }
    GridLayout { columns: 2; Item {} Item {} }
    ColumnLayout { Item {} }
}
)");
}

TEST(QtImports, EffectsLoad) {
  expectLoads(R"(
import QtQuick
import QtQuick.Effects
Item {
    width: 20; height: 20
    Rectangle { id: src; anchors.fill: parent; color: "red"; layer.enabled: true; visible: false }
    MultiEffect { source: src; anchors.fill: parent; maskEnabled: false }
}
)");
}

TEST(QtImports, FolderListModelLoads) {
  expectLoads(R"(
import QtQuick
import Qt.labs.folderlistmodel
Item { FolderListModel { folder: "file:///" } }
)");
}

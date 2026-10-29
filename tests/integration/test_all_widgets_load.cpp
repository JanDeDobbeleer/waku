// Every built-in widget of every feature compiles and resolves its imports. The components are
// created from the module's resource path but not instantiated: creating a Volume or a Media widget
// builds the real audio and media-session services, which a test must not do. The widgets that can
// run on fakes are instantiated in their feature's own tests.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStringList>
#include <QTest>
#include <QUrl>

namespace {

// The names the module lists in its qmldir ("Name 1.0 Name.qml" lines).
QStringList moduleWidgets() {
  QStringList names;
  QFile qmldir(QStringLiteral(":/qt/qml/wabar/widgets/qmldir"));
  if (!qmldir.open(QIODevice::ReadOnly)) return names;
  const QStringList lines = QString::fromUtf8(qmldir.readAll()).split(QLatin1Char('\n'));
  for (const QString& line : lines) {
    const QStringList words = line.simplified().split(QLatin1Char(' '));
    if (words.size() == 3 && words.at(2).endsWith(QLatin1String(".qml"))) names << words.at(0);
  }
  return names;
}

}  // namespace

TEST(BuiltInWidgets, EveryFeatureContributesItsWidgets) {
  const QStringList names = moduleWidgets();
  // One headline widget per feature that has one (brightness and shell are services only).
  for (const char* expected :
       {"Volume", "Bluetooth", "Clock", "ControlCenter", "GitHub", "Media", "Wifi", "Notifications",
        "Cpu", "Taskbar", "Tray", "Wallpapers", "Workspaces", "Pill", "SafeConfig"}) {
    EXPECT_TRUE(names.contains(QString::fromLatin1(expected))) << expected;
  }
}

TEST(BuiltInWidgets, EveryWidgetOfTheModuleCompiles) {
  const QStringList names = moduleWidgets();
  ASSERT_FALSE(names.isEmpty());
  QQmlEngine engine;
  for (const QString& name : names) {
    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/qt/qml/wabar/widgets/%1.qml").arg(name)));
    for (int i = 0; i < 100 && component.isLoading(); ++i) QTest::qWait(50);
    EXPECT_TRUE(component.isReady())
        << qPrintable(name) << ": " << qPrintable(component.errorString());
  }
}

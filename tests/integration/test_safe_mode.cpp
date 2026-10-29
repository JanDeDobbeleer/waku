#include <gtest/gtest.h>

#include "host/testing/bar_fixtures.h"

namespace {

// A tiny safe config, so the tests start none of the real services the built-in one uses
// (Workspaces and Volume would start komorebi, GlazeWM and audio clients).
const char* kTinySafe = R"(
import QtQuick
import wabar
Config {
    Bar {
        layout: BarLayout {
            start: [ Text { objectName: "safeText"; text: "safe" } ]
        }
    }
}
)";

void useTinySafeConfig(Harness& h) {
  const QString path = h.dir.filePath(QStringLiteral("safe-config.qml"));
  write(path, QString::fromLatin1(kTinySafe));
  h.safe = true;
  h.safeUrl = QUrl::fromLocalFile(path);
}

}  // namespace

TEST(SafeMode, BuildsTheSafeConfigAndIgnoresTheConfigHome) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeConfig(h);
  h.start(QStringLiteral("this bar.qml is not valid {{{"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("safeText")), QStringLiteral("safe"));
}

TEST(SafeMode, WorksWithoutAnyFileInTheConfigHome) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeConfig(h);
  h.makeHost();  // no bar.qml at all
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_TRUE(h.host->error().isEmpty());
}

TEST(SafeMode, TheThemeFileIsIgnored) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeConfig(h);
  write(h.dir.filePath(QStringLiteral("theme.qml")),
        QStringLiteral("import wabar\nThemeSpec { accent: \"#ff0000\"; fontSize: 30 }\n"));
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  auto* theme =
      h.engine.singletonInstance<Theme*>(QStringLiteral("wabar"), QStringLiteral("Theme"));
  ASSERT_NE(theme, nullptr);
  EXPECT_EQ(theme->property("accent").value<QColor>(), QColor(0x5b, 0x8d, 0xef));
  EXPECT_EQ(theme->property("fontSize").toInt(), 13);
}

TEST(SafeMode, ABrokenThemeFileDoesNotBreakIt) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeConfig(h);
  write(h.dir.filePath(QStringLiteral("theme.qml")), QStringLiteral("not qml {{{"));
  h.makeHost();
  EXPECT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
}

TEST(SafeMode, ReloadingRebuildsTheSameBar) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeConfig(h);
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("safeText")), QStringLiteral("safe"));
}

TEST(SafeMode, TheConfigHomeIsNotWatched) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeConfig(h);
  write(h.barFile(), barConfig(QStringLiteral("a")));
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &BarHost::reloaded);
  write(h.barFile(), barConfig(QStringLiteral("changed")));
  QTest::qWait(500);  // more than the debounce
  EXPECT_EQ(reloaded.count(), 0);
}

TEST(SafeMode, AFailingSafeConfigStillGetsTheFallbackBar) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  h.safe = true;
  h.safeUrl = QUrl::fromLocalFile(h.dir.filePath(QStringLiteral("missing.qml")));
  h.makeHost();
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  EXPECT_EQ(h.host->windows().size(), 1);  // the fallback bar
}

TEST(SafeMode, TheBuiltInSafeConfigIsAValidConfig) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  h.makeHost();
  EXPECT_EQ(BarHost::safeConfigUrl().toString(),
            QStringLiteral("qrc:/qt/qml/wabar/widgets/SafeConfig.qml"));
  QString error;
  // Checking builds no widget and starts no service: layouts are lazy components.
  EXPECT_TRUE(h.host->checkConfig(BarHost::safeConfigUrl(), &error)) << qPrintable(error);
}

TEST(CheckConfig, AcceptsAGoodConfigAndExplainsABadOne) {
  Harness h;
  h.start(barConfig());
  QString error;
  EXPECT_TRUE(h.host->checkConfig(QUrl::fromLocalFile(h.barFile()), &error)) << qPrintable(error);

  write(h.barFile(), QStringLiteral("not qml {{{"));
  EXPECT_FALSE(h.host->checkConfig(QUrl::fromLocalFile(h.barFile()), &error));
  EXPECT_TRUE(error.startsWith(QStringLiteral("bar.qml:"))) << qPrintable(error);

  write(h.barFile(), QStringLiteral("import QtQuick\nItem {}\n"));
  EXPECT_FALSE(h.host->checkConfig(QUrl::fromLocalFile(h.barFile()), &error));
  EXPECT_TRUE(error.contains(QStringLiteral("must declare a Config"))) << qPrintable(error);
}

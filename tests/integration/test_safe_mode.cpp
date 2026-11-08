#include <gtest/gtest.h>

#include "host/testing/shell_fixtures.h"

namespace {

// A tiny safe shell, so the tests start none of the real services the built-in one uses
// (Volume would start an audio client).
const char* kTinySafe = R"(
import QtQuick
import waku
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [ Text { objectName: "safeText"; text: "safe" } ]
        }
    }
}
)";

void useTinySafeShell(Harness& h) {
  const QString path = h.dir.filePath(QStringLiteral("safe-shell.qml"));
  write(path, QString::fromLatin1(kTinySafe));
  h.safe = true;
  h.safeUrl = QUrl::fromLocalFile(path);
}

}  // namespace

TEST(SafeMode, BuildsTheSafeShellAndIgnoresTheConfigHome) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeShell(h);
  h.start(QStringLiteral("this shell.qml is not valid {{{"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("safeText")), QStringLiteral("safe"));
}

TEST(SafeMode, WorksWithoutAnyFileInTheConfigHome) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeShell(h);
  h.makeHost();  // no shell.qml at all
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_TRUE(h.host->error().isEmpty());
}

TEST(SafeMode, TheThemeFileIsIgnored) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeShell(h);
  write(h.dir.filePath(QStringLiteral("theme.qml")),
        QStringLiteral("import waku\nThemeSpec { accent: \"#ff0000\"; fontSize: 30 }\n"));
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  auto* theme = h.engine.singletonInstance<Theme*>(QStringLiteral("waku"), QStringLiteral("Theme"));
  ASSERT_NE(theme, nullptr);
  EXPECT_EQ(theme->property("accent").value<QColor>(), QColor(0x5b, 0x8d, 0xef));
  EXPECT_EQ(theme->property("fontSize").toInt(), 13);
}

TEST(SafeMode, ABrokenThemeFileDoesNotBreakIt) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeShell(h);
  write(h.dir.filePath(QStringLiteral("theme.qml")), QStringLiteral("not qml {{{"));
  h.makeHost();
  EXPECT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
}

TEST(SafeMode, ReloadingRebuildsTheSameShell) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeShell(h);
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("safeText")), QStringLiteral("safe"));
}

TEST(SafeMode, TheConfigHomeIsNotWatched) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinySafeShell(h);
  write(h.shellFile(), shellConfig(QStringLiteral("a")));
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  // Nothing is watched, so no change event can come and start a reload.
  EXPECT_TRUE(h.host->watchedPaths().isEmpty());
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);
  write(h.shellFile(), shellConfig(QStringLiteral("changed")));
  QCoreApplication::processEvents();
  EXPECT_FALSE(h.host->configReloadPending());
  EXPECT_EQ(h.host->configChangeCount(), 0);
  EXPECT_EQ(reloaded.count(), 0);
}

TEST(SafeMode, AFailingSafeShellStillGetsTheFallbackShell) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  h.safe = true;
  h.safeUrl = QUrl::fromLocalFile(h.dir.filePath(QStringLiteral("missing.qml")));
  h.makeHost();
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  EXPECT_EQ(h.host->windows().size(), 1);  // the fallback shell
}

TEST(SafeMode, TheBuiltInSafeShellIsAValidShell) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  h.makeHost();
  EXPECT_EQ(ShellHost::safeShellUrl().toString(),
            QStringLiteral("qrc:/qt/qml/waku/widgets/SafeShell.qml"));
}

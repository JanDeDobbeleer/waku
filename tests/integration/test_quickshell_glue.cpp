// The Quickshell singleton joined to the shell host the way wabar.exe does it.
#include <gtest/gtest.h>

#include "app/wabar/quickshell_glue.h"
#include "host/testing/shell_fixtures.h"

TEST(QuickshellGlue, ShellDirIsTheConfigHomeAndReloadReloadsTheShell) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell
import wabar

ShellRoot {
    QtObject {
        objectName: "probe"
        readonly property string dir: Quickshell.shellDir
        property int completed: 0
        property string failure: ""
        property Connections c: Connections {
            target: Quickshell
            function onReloadCompleted() { probe.completed += 1 }
            function onReloadFailed(error) { probe.failure = error }
        }
        id: probe
    }
    Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "first" } ] } }
}
)"));
  wabar::app::connectQuickshell(&h.engine, h.host.get(), h.dir.path());
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  auto* probe = h.host->shell()->findChild<QObject*>(QStringLiteral("probe"));
  ASSERT_NE(probe, nullptr);
  EXPECT_EQ(QDir::fromNativeSeparators(probe->property("dir").toString()),
            QDir::fromNativeSeparators(h.dir.path()));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("first"));

  // Edit the file, then ask for the reload from QML-side API (a queued request).
  write(h.shellFile(), QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot { Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "second" } ] } } }
)"));
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);
  // The engine drops its singletons on each reload: resolve the Quickshell one again every time.
  const auto quickshellOf = [&h] {
    return h.engine.singletonInstance<wabar::quickshell_io::QuickshellSingleton*>(
        QStringLiteral("wabar.services"), QStringLiteral("QuickshellSingleton"));
  };
  ASSERT_NE(quickshellOf(), nullptr);
  quickshellOf()->reload();
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 3000));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("second"));

  // The new singleton is wired too, and the outcome reaches its signals.
  auto* quickshell = quickshellOf();
  ASSERT_NE(quickshell, nullptr);
  EXPECT_EQ(QDir::fromNativeSeparators(quickshell->shellDir()),
            QDir::fromNativeSeparators(h.dir.path()));
  QSignalSpy failed(quickshell, &wabar::quickshell_io::QuickshellSingleton::reloadFailed);
  write(h.shellFile(), QStringLiteral("not qml {{{"));
  quickshell->reload(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return failed.count() >= 1; }, 3000));
  EXPECT_FALSE(failed.first().first().toString().isEmpty());
}

// The Quickshell singleton joined to the shell host the way waku.exe does it.
#include <gtest/gtest.h>

#include <QClipboard>
#include <QGuiApplication>

#include "app/waku/quickshell_glue.h"
#include "features/quickshell_io/qml/clipboard_text.h"
#include "host/testing/shell_fixtures.h"

TEST(QuickshellGlue, ShellDirIsTheConfigHomeAndReloadReloadsTheShell) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell
import waku

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
  waku::app::connectQuickshell(&h.engine, h.host.get(), h.dir.path());
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  auto* probe = h.host->shell()->findChild<QObject*>(QStringLiteral("probe"));
  ASSERT_NE(probe, nullptr);
  EXPECT_EQ(QDir::fromNativeSeparators(probe->property("dir").toString()),
            QDir::fromNativeSeparators(h.dir.path()));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("first"));

  // Edit the file, then ask for the reload from QML-side API (a queued request).
  write(h.shellFile(), QStringLiteral(R"(
import QtQuick
import waku
ShellRoot { Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "second" } ] } } }
)"));
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);
  // The engine drops its singletons on each reload: resolve the Quickshell one again every time.
  const auto quickshellOf = [&h] {
    return h.engine.singletonInstance<waku::quickshell_io::QuickshellSingleton*>(
        QStringLiteral("waku.services"), QStringLiteral("QuickshellSingleton"));
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
  // A failed reload resets the engine too, so the singleton that reports it is a new one: listen
  // to it as soon as it exists.
  QString failure;
  QObject::connect(h.host.get(), &ShellHost::singletonsCleared, &h.engine, [&] {
    if (auto* fresh = quickshellOf()) {
      QObject::connect(fresh, &waku::quickshell_io::QuickshellSingleton::reloadFailed, &h.engine,
                       [&failure](const QString& error) { failure = error; });
    }
  });
  write(h.shellFile(), QStringLiteral("not qml {{{"));
  quickshell->reload(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !failure.isEmpty(); }, 3000));
}

// Creating the Quickshell singleton (start and every reload) must not read the system clipboard:
// a read can block the GUI thread for seconds. A shell that reads clipboardText reads it then.
TEST(QuickshellGlue, StartAndReloadDoNotReadTheClipboard) {
  QGuiApplication::clipboard()->setText(QStringLiteral("glue clip"));
  const int start = waku::quickshell_io::ClipboardText::readsForTesting();
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell
import waku

ShellRoot {
    QtObject {
        id: probe
        objectName: "probe"
        readonly property string dir: Quickshell.shellDir
        function clip() { return Quickshell.clipboardText }
        function setClip(text) { Quickshell.clipboardText = text }
    }
    Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "first" } ] } }
}
)"));
  waku::app::connectQuickshell(&h.engine, h.host.get(), h.dir.path());
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  EXPECT_EQ(waku::quickshell_io::ClipboardText::readsForTesting(), start);

  auto* probe = h.host->shell()->findChild<QObject*>(QStringLiteral("probe"));
  ASSERT_NE(probe, nullptr);
  QVariant text;
  ASSERT_TRUE(QMetaObject::invokeMethod(probe, "clip", Q_RETURN_ARG(QVariant, text)));
  EXPECT_EQ(text.toString(), QStringLiteral("glue clip"));
  EXPECT_EQ(waku::quickshell_io::ClipboardText::readsForTesting(), start + 1);

  // A write from QML reaches the system clipboard and reads back.
  ASSERT_TRUE(QMetaObject::invokeMethod(probe, "setClip", Q_ARG(QVariant, QStringLiteral("set"))));
  EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("set"));
  ASSERT_TRUE(QMetaObject::invokeMethod(probe, "clip", Q_RETURN_ARG(QVariant, text)));
  EXPECT_EQ(text.toString(), QStringLiteral("set"));
}

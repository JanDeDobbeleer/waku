#include <gtest/gtest.h>

#include "host/testing/shell_fixtures.h"

// ---- ShellRoot ----

TEST(ShellRoot, LoadsAndKeepsWhatItDeclaresInOrder) {
  QQmlEngine engine;
  QTemporaryDir dir;
  write(dir.filePath(QStringLiteral("shell.qml")), QStringLiteral(R"(
import QtQml
import wabar
ShellRoot {
    QtObject { objectName: "first" }
    Scope { QtObject { objectName: "nested" } }
    QtObject { objectName: "last" }
}
)"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(dir.filePath(QStringLiteral("shell.qml"))));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  auto* shell = qobject_cast<ShellRoot*>(root.get());
  ASSERT_NE(shell, nullptr);
  ASSERT_EQ(shell->children().size(), 3);
  EXPECT_EQ(shell->children()[0]->objectName(), QStringLiteral("first"));
  auto* scope = qobject_cast<Scope*>(shell->children()[1]);
  ASSERT_NE(scope, nullptr);
  ASSERT_EQ(scope->children().size(), 1);
  EXPECT_EQ(scope->children()[0]->objectName(), QStringLiteral("nested"));
  EXPECT_EQ(shell->children()[2]->objectName(), QStringLiteral("last"));
}

TEST(ShellRoot, TheOldNamesAreGone) {
  QQmlEngine engine;
  QTemporaryDir dir;
  for (const char* text : {"import wabar\nConfig {}\n", "import wabar\nShellRoot { Bar {} }\n",
                           "import wabar\nShellRoot { Panel { content: BarLayout {} } }\n"}) {
    write(dir.filePath(QStringLiteral("shell.qml")), QString::fromLatin1(text));
    engine.clearComponentCache();
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(dir.filePath(QStringLiteral("shell.qml"))));
    EXPECT_TRUE(component.isError()) << text;
  }
}

// ---- building windows ----

TEST(ShellHostTest, BuildsOneWindowPerPanelAndMatchingScreen) {
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_TRUE(h.host->error().isEmpty());
  ASSERT_NE(h.host->shell(), nullptr);
}

TEST(ShellHostTest, AllScreensGetAPanel) {
  Harness h;
  h.screenCount = 2;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("screens: [\"all\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 2);
  EXPECT_NE(h.window(0), h.window(1));
}

TEST(ShellHostTest, APanelForAnUnknownScreenOrDisabledBuildsNothing) {
  Harness h;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("screens: [\"DISPLAY9\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->windows().isEmpty());

  Harness d;
  d.start(shellConfig(QStringLiteral("a"), QStringLiteral("enabled: false")));
  ASSERT_TRUE(d.host->reload());
  EXPECT_TRUE(d.host->windows().isEmpty());
}

TEST(ShellHostTest, APanelSpansTheScreenAtItsThickness) {
  Harness h;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("thickness: 32")));
  ASSERT_TRUE(h.host->reload());
  settle();
  const QScreen* screen = QGuiApplication::primaryScreen();
  EXPECT_EQ(h.window()->width(), screen->size().width());
  EXPECT_EQ(h.window()->height(), 32);
  EXPECT_EQ(h.window()->y(), screen->geometry().y());
}

TEST(ShellHostTest, ABottomPanelSitsAtTheBottomEdge) {
  Harness h;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("thickness: 32; edge: Panel.Bottom")));
  ASSERT_TRUE(h.host->reload());
  settle();
  const QScreen* screen = QGuiApplication::primaryScreen();
  EXPECT_EQ(h.window()->y(), screen->geometry().y() + screen->geometry().height() - 32);
}

// ---- PanelLayout ----

TEST(PanelLayoutTest, WidgetsFlowStartCenterEnd) {
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  settle();
  const QPointF a = positionIn(h.window(), QStringLiteral("a"));
  const QPointF b = positionIn(h.window(), QStringLiteral("b"));
  const QPointF c = positionIn(h.window(), QStringLiteral("c"));
  EXPECT_LT(a.x(), b.x());
  EXPECT_LT(b.x(), c.x());
  EXPECT_DOUBLE_EQ(a.x(), 12);  // the default padding
  const double width = h.window()->width();
  EXPECT_NEAR(b.x() + findItem(h.window(), QStringLiteral("b"))->width() / 2, width / 2, 1);
  EXPECT_NEAR(c.x() + findItem(h.window(), QStringLiteral("c"))->width(), width - 12, 1);
}

TEST(PanelLayoutTest, AHiddenWidgetTakesNoSpace) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [ Text { objectName: "hidden"; text: "x"; visible: false },
                     Text { objectName: "a"; text: "a" } ]
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  settle();
  EXPECT_DOUBLE_EQ(positionIn(h.window(), QStringLiteral("a")).x(), 12);
}

// ---- the native layer ----

TEST(ShellHostTest, AppliesTheBackdropAndSetsTheMonitorHandle) {
  Harness h;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("backdrop: Backdrop.Blur")));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.native.backdrops.size(), 1u);
  EXPECT_EQ(h.native.backdrops[0], Backdrop::Blur);
  ASSERT_NE(h.shellWindow(), nullptr);
  EXPECT_EQ(h.shellWindow()->monitorHandle(), 4242);
}

TEST(ShellHostTest, RegistersAnAppBarWithTheEdgeAndThickness) {
  Harness h;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("thickness: 30; edge: Panel.Bottom")));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.native.created.size(), 1u);
  EXPECT_EQ(h.native.created[0].request.edge, wabar::host::AppBarEdge::Bottom);
  EXPECT_EQ(h.native.created[0].request.height, 30);
  EXPECT_TRUE(h.native.created[0].request.alwaysOnTop);
  EXPECT_TRUE(h.native.created[0].request.noActivate);
}

TEST(ShellHostTest, ReservesSpaceOnlyWhenThePanelAsksAndTheHostAllows) {
  const struct {
    bool allow;
    bool ask;
    bool reserved;
  } cases[] = {
      {false, false, false}, {false, true, false}, {true, false, false}, {true, true, true}};
  for (const auto& c : cases) {
    Harness h;
    h.allowReserve = c.allow;
    h.start(shellConfig(QStringLiteral("a"),
                        QStringLiteral("exclusive: ") + (c.ask ? "true" : "false")));
    ASSERT_TRUE(h.host->reload());
    ASSERT_EQ(h.native.created.size(), 1u);
    EXPECT_EQ(h.native.created[0].request.reserveSpace, c.reserved)
        << "allow=" << c.allow << " ask=" << c.ask;
  }
}

TEST(ShellHostTest, DestroyingTheHostRemovesItsAppBars) {
  Harness h;
  h.screenCount = 2;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("screens: [\"all\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.native.removed, 0);
  h.host.reset();
  EXPECT_EQ(h.native.removed, 2);
}

// ---- reload ----

TEST(ShellHostTest, ReloadReplacesTheWindowsWithTheNewShell) {
  Harness h;
  h.start(shellConfig(QStringLiteral("first")));
  ASSERT_TRUE(h.host->reload());
  QPointer<QQuickWindow> old = h.window();
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("first"));

  write(h.shellFile(), shellConfig(QStringLiteral("second")));
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(reloaded.count(), 1);
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("second"));
  QTest::qWait(10);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_TRUE(old.isNull());
  EXPECT_EQ(h.native.removed, 1);  // the old window's app bar went with it
}

TEST(ShellHostTest, ABrokenReloadKeepsThePreviousShellAndReportsTheError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  QQuickWindow* before = h.window();
  QSignalSpy errorChanged(h.host.get(), &ShellHost::errorChanged);

  write(h.shellFile(), shellConfig(QStringLiteral("x")) + QStringLiteral("\nthis is not qml {{{"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.window(), before);  // the very same window
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("good"));
  EXPECT_EQ(errorChanged.count(), 1);
  EXPECT_TRUE(h.host->error().startsWith(QStringLiteral("shell.qml:")))
      << qPrintable(h.host->error());

  // The pill is visible in the kept window.
  settle();
  QQuickItem* pill = findItem(before, QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

TEST(ShellHostTest, FixingTheShellClearsTheError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.shellFile(), QStringLiteral("not qml {{{"));
  ASSERT_FALSE(h.host->reload());
  ASSERT_FALSE(h.host->error().isEmpty());

  write(h.shellFile(), shellConfig(QStringLiteral("fixed")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("fixed"));
  settle();
  EXPECT_FALSE(findItem(h.window(), QStringLiteral("errorPill"))->isVisible());
}

TEST(ShellHostTest, ARootThatIsNotAShellRootIsAnError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.shellFile(), QStringLiteral("import QtQuick\nItem {}\n"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("must declare a ShellRoot")))
      << qPrintable(h.host->error());
  EXPECT_EQ(h.host->windows().size(), 1);  // still the old shell
}

TEST(ShellHostTest, AnUnknownWidgetTypeIsAnError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.shellFile(), QStringLiteral(R"(
import wabar
ShellRoot { Panel { content: PanelLayout { start: [ NoSuchWidget {} ] } } }
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("good"));
}

TEST(ShellHostTest, CheckingAShellCreatesNothing) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    PanelWindow { objectName: "probe"; Text { text: "x" } }
}
)"));
  QString error;
  const int before = static_cast<int>(QGuiApplication::allWindows().size());
  EXPECT_TRUE(h.host->checkShell(QUrl::fromLocalFile(h.shellFile()), &error)) << qPrintable(error);
  EXPECT_EQ(static_cast<int>(QGuiApplication::allWindows().size()), before);
}

TEST(ShellHostTest, ABrokenFirstLoadShowsAFallbackShellWithTheError) {
  Harness h;
  h.start(QStringLiteral("not qml {{{"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  ASSERT_EQ(h.host->windows().size(), 1);  // the fallback shell
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

TEST(ShellHostTest, AMissingShellFileIsAnErrorWithAFallbackShell) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  h.makeHost();  // no shell.qml was written
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("shell.qml not found")))
      << qPrintable(h.host->error());
  EXPECT_EQ(h.host->windows().size(), 1);
}

TEST(ShellHostTest, AnOldBarQmlNamesTheRename) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  write(h.dir.filePath(QStringLiteral("bar.qml")),
        QStringLiteral("import wabar\nConfig { Bar {} }\n"));
  h.makeHost();
  EXPECT_FALSE(h.host->reload());
  const QString error = h.host->error();
  EXPECT_TRUE(error.contains(QStringLiteral("bar.qml"))) << qPrintable(error);
  EXPECT_TRUE(error.contains(QStringLiteral("rename it to shell.qml"))) << qPrintable(error);
  EXPECT_TRUE(error.contains(QStringLiteral("ShellRoot"))) << qPrintable(error);
  ASSERT_EQ(h.host->windows().size(), 1);  // the fallback shell shows it
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

// ---- one broken window does not take the others down (proposal 4.1.3) ----

TEST(ShellHostTest, AWindowThatFailsToBuildLeavesTheOthersStanding) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    Panel {
        content: Item { objectName: "good" }
    }
    Panel {
        edge: Panel.Bottom
        content: Item { objectName: "bad"; required property int notGiven }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_NE(findItem(h.window(), QStringLiteral("good")), nullptr);
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("notGiven"))) << qPrintable(h.host->error());
  EXPECT_EQ(h.native.created.size(), 1u);  // only the built window became an app bar
  settle();
  EXPECT_TRUE(findItem(h.window(), QStringLiteral("errorPill"))->isVisible());
}

TEST(ShellHostTest, WhenNoWindowBuildsTheFallbackShows) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    Panel { content: Item { objectName: "bad"; required property int notGiven } }
}
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("notGiven"))) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(findItem(h.window(), QStringLiteral("bad")), nullptr);
}

// ---- watching ----

TEST(ShellHostTest, ChangingAFileInTheConfigHomeReloads) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);

  write(h.shellFile(), shellConfig(QStringLiteral("after")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
}

TEST(ShellHostTest, ChangingAWidgetFileInASubfolderReloads) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  QDir(h.dir.path()).mkdir(QStringLiteral("widgets"));
  const QString widget = h.dir.filePath(QStringLiteral("widgets/Label.qml"));
  write(widget, QStringLiteral("import QtQuick\nText { objectName: \"w\"; text: \"first\" }\n"));
  h.start(QStringLiteral(R"(
import wabar
import "widgets"
ShellRoot { Panel { content: PanelLayout { start: [ Label {} ] } } }
)"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.text(QStringLiteral("w")), QStringLiteral("first"));
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);

  write(widget, QStringLiteral("import QtQuick\nText { objectName: \"w\"; text: \"second\" }\n"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_TRUE(h.host->error().isEmpty()) << qPrintable(h.host->error());
  EXPECT_EQ(h.text(QStringLiteral("w")), QStringLiteral("second"));
}

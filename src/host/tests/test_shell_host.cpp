#include <gtest/gtest.h>

#include <QMouseEvent>

#include "host/qml/panel_window.h"
#include "host/testing/shell_fixtures.h"

// ---- ShellRoot ----

TEST(ShellRoot, LoadsAndKeepsWhatItDeclaresInOrder) {
  QQmlEngine engine;
  QTemporaryDir dir;
  write(dir.filePath(QStringLiteral("shell.qml")), QStringLiteral(R"(
import QtQml
import waku
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
  for (const char* text : {"import waku\nConfig {}\n", "import waku\nShellRoot { Bar {} }\n",
                           "import waku\nShellRoot { Panel { content: BarLayout {} } }\n"}) {
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

TEST(ShellHostTest, APressOnAShellWindowIsReportedAndOtherWindowsAreNot) {
  Harness h;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.host->windows().size(), 1);
  int presses = 0;
  QObject::connect(h.host.get(), &ShellHost::windowPressed, [&presses] { ++presses; });
  const auto press = [](QWindow* window) {
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
  };
  press(h.host->windows().first());
  EXPECT_EQ(presses, 1);
  QQuickWindow other;
  press(&other);
  EXPECT_EQ(presses, 1);
}

TEST(ShellHostTest, AScreenChangeReloadsTheShellOnceAfterItSettles) {
  Harness h;
  h.screenSettleMs = 200;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  int reloads = 0;
  QObject::connect(h.host.get(), &ShellHost::reloaded, [&reloads] { ++reloads; });
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  emit qGuiApp->screenAdded(screen);
  emit qGuiApp->primaryScreenChanged(screen);
  EXPECT_TRUE(h.host->screenReloadPending());
  EXPECT_EQ(reloads, 0);
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloads > 0; }, 5000));
  // The two changes shared one wait, and nothing restarted it: no second reload is coming.
  EXPECT_FALSE(h.host->screenReloadPending());
  EXPECT_EQ(reloads, 1);
}

TEST(ShellHostTest, AScaleChangeThatMovesOnlyTheGeometryReloadsTheShell) {
  // Windows reports a scale change as a new device pixel ratio and a new logical size, with the
  // logical DPI unchanged: only geometryChanged tells.
  Harness h;
  h.screenSettleMs = 200;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  int reloads = 0;
  QObject::connect(h.host.get(), &ShellHost::reloaded, [&reloads] { ++reloads; });
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  emit qGuiApp->screenAdded(screen);  // makes the host watch this screen
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloads > 0; }, 5000));
  reloads = 0;
  emit screen->geometryChanged(screen->geometry());
  EXPECT_TRUE(h.host->screenReloadPending());
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloads > 0; }, 5000));
  EXPECT_EQ(reloads, 1);
}

TEST(ShellHostTest, AScreenChangeWaitsForADockToSettle) {
  EXPECT_EQ(ShellHost::Options().screenSettleMs, 1500);  // a dock sends changes for 1 to 3 s
  Harness h;
  h.screenSettleMs = 600;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  int reloads = 0;
  QObject::connect(h.host.get(), &ShellHost::reloaded, [&reloads] { ++reloads; });
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  emit qGuiApp->screenAdded(screen);
  ASSERT_TRUE(h.host->screenReloadPending());
  // Part of the wait goes by; the margin to the end (500 ms) only has to outlast one pass of the
  // event loop.
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.host->screenReloadRemainingMs() < 500; }, 5000));
  EXPECT_EQ(reloads, 0);  // a dock is still sending changes
  emit qGuiApp->primaryScreenChanged(screen);
  EXPECT_GT(h.host->screenReloadRemainingMs(), 500);  // the next change starts the wait again
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloads > 0; }, 5000));
  EXPECT_FALSE(h.host->screenReloadPending());
  EXPECT_EQ(reloads, 1);
}

TEST(ShellHostTest, AReloadLeavesNoWindowOfTheOldShellAlive) {
  // Variants and LazyLoader release their windows with deleteLater; the reload deletes them before
  // it clears the engine's singletons and builds the next shell.
  Harness h;
  h.screenCount = 2;
  h.start(QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Variants {
        model: ScreenList.screens
        PanelWindow {
            required property ShellScreen modelData
            screen: modelData
            anchors { top: true; left: true; right: true }
            implicitHeight: 20
            Text { text: modelData.name }
        }
    }
    LazyLoader {
        objectName: "loader"
        active: true
        FloatingWindow { implicitWidth: 100; implicitHeight: 80; Text { text: "lazy" } }
    }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 3);
  QList<QPointer<QsWindow>> old;
  QList<QPointer<QQuickWindow>> oldBacking;
  for (int i = 0; i < 3; ++i) {
    old << h.shellWindow(i);
    oldBacking << h.window(i);
  }
  int seen = 0;
  // The old windows are gone by the time the new shell's singletons exist.
  QObject::connect(h.host.get(), &ShellHost::singletonsCleared, [&] {
    for (const QPointer<QsWindow>& window : old) seen += window ? 1 : 0;
  });
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  EXPECT_EQ(seen, 0);
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(old.at(i).isNull()) << i;
    EXPECT_TRUE(oldBacking.at(i).isNull()) << i;
  }
  EXPECT_EQ(h.host->windows().size(), 3);
}

TEST(ShellHostTest, AllScreensGetAPanel) {
  Harness h;
  h.screenCount = 2;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 2);
  EXPECT_NE(h.window(0), h.window(1));
}

TEST(ShellHostTest, PrimaryScreenCanBeSelectedExplicitly) {
  Harness h;
  h.screenCount = 2;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("screens: [\"primary\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
}

TEST(ShellHostTest, FullscreenLowersOnlyTheAffectedPanelAndRestoresIt) {
  Harness h;
  h.screenCount = 2;
  h.native.fullscreenHook = true;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.host->windows().size(), 2);
  const auto first = static_cast<quintptr>(h.window(0)->winId());
  const auto second = static_cast<quintptr>(h.window(1)->winId());
  h.native.raised.clear();
  h.native.fullscreenPanels.insert(second);
  QTRY_COMPARE_WITH_TIMEOUT(h.native.lowered.size(), 1u, 2000);
  EXPECT_EQ(h.native.lowered[0], second);
  EXPECT_TRUE(h.native.raised.empty());
  h.host->windowShown(h.shellWindow(1), false);
  EXPECT_TRUE(h.native.raised.empty());
  h.native.fullscreenPanels.clear();
  QTRY_COMPARE_WITH_TIMEOUT(h.native.raised.size(), 1u, 2000);
  EXPECT_EQ(h.native.raised[0], second);
  EXPECT_NE(first, second);
}

TEST(ShellHostTest, FullscreenDoesNotRestoreAPanelThatChangedToTheBottomLayer) {
  Harness h;
  h.native.fullscreenHook = true;
  h.start(shellConfig());
  ASSERT_TRUE(h.host->reload());
  const auto hwnd = static_cast<quintptr>(h.window()->winId());
  h.native.fullscreenPanels.insert(hwnd);
  QTRY_COMPARE_WITH_TIMEOUT(h.native.lowered.size(), 1u, 2000);
  h.native.raised.clear();
  auto* panel = qobject_cast<PanelWindow*>(h.shellWindow());
  ASSERT_NE(panel, nullptr);
  panel->setLayer(WlrLayer::Bottom);
  h.native.fullscreenPanels.clear();
  QTest::qWait(600);
  EXPECT_TRUE(h.native.raised.empty());
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
import waku
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
  EXPECT_EQ(h.native.created[0].request.edge, waku::host::AppBarEdge::Bottom);
  EXPECT_EQ(h.native.created[0].request.thickness, 30);
  EXPECT_TRUE(h.native.created[0].request.alwaysOnTop);
  EXPECT_TRUE(h.native.created[0].request.noActivate);
}

TEST(ShellHostTest, TheGrantedAppBarInsetSurvivesALaterGeometryPass) {
  Harness h;
  h.allowReserve = true;
  h.native.appBarInset = 48;  // another app bar holds the top 48 px
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("exclusive: true; thickness: 30")));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.native.created.size(), 1u);
  const QRect screen = QGuiApplication::primaryScreen()->geometry();
  EXPECT_EQ(h.window()->geometry().y(), screen.y() + 48);
  // A margin change makes the panel place itself again; it stays where the shell put it.
  QsWindow* window = h.shellWindow();
  window->setProperty("implicitHeight", 36);
  EXPECT_EQ(h.window()->geometry().y(), screen.y() + 48);
  window->setProperty("implicitWidth", 31);
  EXPECT_EQ(h.window()->geometry().y(), screen.y() + 48);
}

TEST(ShellHostTest, AThicknessChangeAsksTheShellForTheAppBarRectAgain) {
  Harness h;
  h.allowReserve = true;
  h.native.appBarInset = 10;
  h.start(shellConfig(QStringLiteral("a"), QStringLiteral("exclusive: true; thickness: 30")));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.native.created.size(), 1u);
  EXPECT_TRUE(h.native.repositioned.empty());
  h.shellWindow()->setProperty("exclusiveZone", 44);
  ASSERT_EQ(h.native.repositioned.size(), 1u);
  EXPECT_EQ(h.native.repositioned[0].request.thickness, 44);
  EXPECT_EQ(h.native.created.size(), 1u);
  // The same layout again asks nothing more.
  h.shellWindow()->setProperty("exclusiveZone", 44);
  EXPECT_EQ(h.native.repositioned.size(), 1u);
  // The grant changes with the next answer.
  h.native.appBarInset = 20;
  h.shellWindow()->setProperty("exclusiveZone", 50);
  EXPECT_EQ(h.native.repositioned.size(), 2u);
  EXPECT_EQ(h.window()->geometry().y(), QGuiApplication::primaryScreen()->geometry().y() + 20);
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
  EXPECT_TRUE(QTest::qWaitFor(
      [&] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return old.isNull();
      },
      2000));
  EXPECT_EQ(h.native.removed, 1);  // the old window's app bar went with it
}

TEST(ShellHostTest, ABrokenReloadShowsTheFallbackWithTheError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  QSignalSpy errorChanged(h.host.get(), &ShellHost::errorChanged);

  write(h.shellFile(), shellConfig(QStringLiteral("x")) + QStringLiteral("\nthis is not qml {{{"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("<missing>"));  // the old shell is gone
  EXPECT_EQ(errorChanged.count(), 1);
  EXPECT_TRUE(h.host->error().startsWith(QStringLiteral("shell.qml:")))
      << qPrintable(h.host->error());

  // The pill is visible in the fallback window.
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

TEST(ShellHostTest, AMismatchedPluginBesideTheShellRefusesTheReload) {
  Harness h;
  h.start(shellConfig());
  const QString lib = h.dir.filePath(QStringLiteral("lib"));
  ASSERT_TRUE(QDir().mkpath(lib));
  write(QDir(lib).filePath(QStringLiteral("qmldir")),
        QStringLiteral("module Lib\nplugin FakePlugin\n"));
  write(QDir(lib).filePath(QStringLiteral("FakePlugin.dll")), QStringLiteral("not a library"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("FakePlugin.dll")))
      << qPrintable(h.host->error());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("refused")));
  // Removing the plugin file clears the problem: the engine reports a missing plugin itself.
  QFile::remove(QDir(lib).filePath(QStringLiteral("FakePlugin.dll")));
  EXPECT_TRUE(h.host->reload()) << qPrintable(h.host->error());
}

TEST(ShellHostTest, AQmldirWithoutAPluginDoesNotBlockTheReload) {
  Harness h;
  h.start(shellConfig());
  const QString lib = h.dir.filePath(QStringLiteral("lib"));
  ASSERT_TRUE(QDir().mkpath(lib));
  write(QDir(lib).filePath(QStringLiteral("qmldir")), QStringLiteral("module Lib\n"));
  EXPECT_TRUE(h.host->reload()) << qPrintable(h.host->error());
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
  EXPECT_EQ(h.host->windows().size(), 1);  // the fallback shell
}

TEST(ShellHostTest, AnUnknownWidgetTypeIsAnError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.shellFile(), QStringLiteral(R"(
import waku
ShellRoot { Panel { content: PanelLayout { start: [ NoSuchWidget {} ] } } }
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  EXPECT_EQ(h.host->windows().size(), 1);                               // the fallback shell
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("<missing>"));  // the old shell is gone
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

TEST(ShellHostTest, AFirstLoadWhoseRootIsNotAShellRootCreatesNothingAndShowsTheFallback) {
  Harness h;
  // build() refuses the root before it creates anything.
  h.start(QStringLiteral(R"(
import QtQuick
import waku
Item { PanelWindow { objectName: "never" } }
)"));
  const auto before = QGuiApplication::allWindows().size();
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("shell.qml must declare a ShellRoot")))
      << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 1);  // the fallback shell
  // Only the fallback's window was made: the PanelWindow inside the Item never was.
  EXPECT_EQ(QGuiApplication::allWindows().size(), before + 1);
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
        QStringLiteral("import waku\nConfig { Bar {} }\n"));
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
import waku
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
import waku
ShellRoot {
    Panel { content: Item { objectName: "bad"; required property int notGiven } }
}
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("notGiven"))) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(findItem(h.window(), QStringLiteral("bad")), nullptr);
}

// The engine was reset: the old shell is gone and the fallback shows why.
TEST(ShellHostTest, AReloadThatFailsWhileCreatingShowsTheFallbackWithTheError) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());

  // It compiles; the content cannot be created.
  write(h.shellFile(), QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Panel { content: Item { objectName: "bad"; required property int notGiven } }
}
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("notGiven"))) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("<missing>"));  // the old shell is gone
  EXPECT_EQ(findItem(h.window(), QStringLiteral("bad")), nullptr);
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

TEST(ShellHostTest, AReloadWhoseVariantsDelegateFailsShowsTheFallback) {
  Harness h;
  h.start(shellConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());

  write(h.shellFile(), QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Variants {
        model: 1
        PanelWindow {
            required property int notGiven
            implicitHeight: 20
        }
    }
}
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("no windows")))
      << qPrintable(h.host->error());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("notGiven"))) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("<missing>"));  // the fallback, not "good"
}

TEST(ShellHostTest, ABrokenFirstLoadThatCompilesStillShowsTheFallback) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Panel { content: Item { required property int notGiven } }
}
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  ASSERT_EQ(h.host->windows().size(), 1);
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
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
import waku
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

TEST(ShellHostTest, WritingANonQmlFileInTheConfigHomeDoesNotReload) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);

  // A shell persists its state next to its files: that must not rebuild the shell. Each change
  // event decides at once whether a reload is due, so after the event nothing is pending.
  int seen = h.host->configChangeCount();
  const QString state = h.dir.filePath(QStringLiteral("state.json"));
  write(state, QStringLiteral("{\"a\":1}"));
  QDir(h.dir.path()).mkdir(QStringLiteral("cache"));
  write(h.dir.filePath(QStringLiteral("cache/entry.bin")), QStringLiteral("x"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.host->configChangeCount() > seen; }, 5000));
  EXPECT_FALSE(h.host->configReloadPending());
  // A later save (replaced, as editors and state stores do) is just as harmless.
  seen = h.host->configChangeCount();
  ASSERT_TRUE(QFile::remove(state));
  write(state, QStringLiteral("{\"a\":2}"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.host->configChangeCount() > seen; }, 5000));
  EXPECT_FALSE(h.host->configReloadPending());
  EXPECT_EQ(reloaded.count(), 0);

  // A .qml edit still reloads, after those writes.
  write(h.shellFile(), shellConfig(QStringLiteral("after")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
}

TEST(ShellHostTest, ANewQmlFileInTheConfigHomeReloads) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);
  write(h.dir.filePath(QStringLiteral("Extra.qml")), QStringLiteral("import QtQml\nQtObject {}\n"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
}

TEST(ShellHostTest, AChangedQmlFileIsComparedByOneStatNotAWalk) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  const int walks = h.host->configWalkCount();

  // The watcher reports the file but it did not change: no walk, no reload.
  h.host->configHomeChanged(h.shellFile());
  EXPECT_EQ(h.host->configWalkCount(), walks);
  EXPECT_FALSE(h.host->configReloadPending());

  // It changed (the size differs): still no walk, a reload is pending.
  write(h.shellFile(), shellConfig(QStringLiteral("after, and longer")));
  h.host->configHomeChanged(h.shellFile());
  EXPECT_EQ(h.host->configWalkCount(), walks);
  EXPECT_TRUE(h.host->configReloadPending());
}

TEST(ShellHostTest, AChangedFolderWalksTheConfigHome) {
  Harness h;
  h.start(shellConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  const int walks = h.host->configWalkCount();
  h.host->configHomeChanged();
  EXPECT_EQ(h.host->configWalkCount(), walks + 1);
  EXPECT_FALSE(h.host->configReloadPending());
}

TEST(ShellHostTest, TheWorkingSetIsTrimmedOnceAfterEachInstall) {
  Harness h;
  h.native.trimHook = true;
  h.trimDelayMs = 20;
  h.start(shellConfig(QStringLiteral("a")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.native.trims, 0);  // not at install: the shell settles first
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.native.trims == 1; }, 2000));
  QTest::qWait(100);
  EXPECT_EQ(h.native.trims, 1);  // a single shot, not a loop
  ASSERT_TRUE(h.host->reload());
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.native.trims == 2; }, 2000));
}

namespace {

// A shell with no window at install and a loader per window that the test activates later.
QString lateWindowsShell() {
  return QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    LazyLoader {
        objectName: "late1"
        FloatingWindow { implicitWidth: 100; implicitHeight: 80; Text { text: "one" } }
    }
    LazyLoader {
        objectName: "late2"
        FloatingWindow { implicitWidth: 100; implicitHeight: 80; Text { text: "two" } }
    }
}
)");
}

void showLate(Harness& h, const char* name) {
  auto* loader = h.host->shell()->findChild<QObject*>(QString::fromLatin1(name));
  ASSERT_NE(loader, nullptr);
  loader->setProperty("active", true);
  settle();
}

}  // namespace

TEST(ShellHostTest, WindowsThatAppearAfterInstallAreTrimmedOnceTheyStayStable) {
  Harness h;
  h.native.trimHook = true;
  h.trimDelayMs = 60;
  h.start(lateWindowsShell());
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 0);
  showLate(h, "late1");
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.native.trims, 0);  // the first look sees a new window and waits
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.native.trims == 1; }, 2000));
  QTest::qWait(200);
  EXPECT_EQ(h.native.trims, 1);
}

TEST(ShellHostTest, NoTrimWhileWindowsKeepAppearing) {
  Harness h;
  h.native.trimHook = true;
  h.trimDelayMs = 150;
  h.start(lateWindowsShell());
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  showLate(h, "late1");  // seen at the first tick
  QTest::qWait(h.trimDelayMs + 60);
  EXPECT_EQ(h.native.trims, 0);
  showLate(h, "late2");  // grows again before the second tick
  ASSERT_EQ(h.host->windows().size(), 2);
  QTest::qWait(h.trimDelayMs);
  EXPECT_EQ(h.native.trims, 0);
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.native.trims == 1; }, 2000));
}

TEST(ShellHostTest, NoTrimHookMeansNoTrim) {
  Harness h;
  h.trimDelayMs = 20;
  h.start(shellConfig(QStringLiteral("a")));
  ASSERT_TRUE(h.host->reload());
  QTest::qWait(100);
  EXPECT_EQ(h.native.trims, 0);
}

// ---- singletons across a reload ----

namespace {

QStringList* g_reloadWarnings = nullptr;
QtMessageHandler g_reloadPrevious = nullptr;

void collectReloadWarnings(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  if (g_reloadWarnings && type != QtDebugMsg) g_reloadWarnings->append(text);
  if (g_reloadPrevious) g_reloadPrevious(type, context, text);
}

// Collects the Qt messages of its lifetime.
struct WarningCapture {
  QStringList lines;
  WarningCapture() {
    g_reloadWarnings = &lines;
    g_reloadPrevious = qInstallMessageHandler(collectReloadWarnings);
  }
  ~WarningCapture() {
    qInstallMessageHandler(g_reloadPrevious);
    g_reloadWarnings = nullptr;
  }
  bool has(const QString& needle) const {
    for (const QString& line : lines) {
      if (line.contains(needle)) return true;
    }
    return false;
  }
};

void writeSingletonShell(Harness& h) {
  write(h.dir.filePath(QStringLiteral("qmldir")), QStringLiteral(R"(
singleton Counter Counter.qml
singleton Holder Holder.qml
Foo Foo.qml
)"));
  write(h.dir.filePath(QStringLiteral("Counter.qml")), QStringLiteral(R"(
pragma Singleton
import QtQml
QtObject { property int count: 0 }
)"));
  // The ScreenState pattern: a singleton whose property type is a component of another file.
  write(h.dir.filePath(QStringLiteral("Holder.qml")), QStringLiteral(R"(
pragma Singleton
import QtQml
QtObject { property Foo state: Foo {} }
)"));
  write(h.shellFile(), QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    QtObject {
        objectName: "probe"
        property int count: Counter.count
        property string label: Holder.state.label
        Component.onCompleted: Counter.count += 1
    }
    Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "x" } ] } }
}
)"));
}

void writeFoo(Harness& h, const QString& label) {
  write(h.dir.filePath(QStringLiteral("Foo.qml")),
        QStringLiteral("import QtQml\nQtObject { property string label: \"%1\" }\n").arg(label));
}

QObject* probeOf(Harness& h) {
  return h.host->shell() ? h.host->shell()->findChild<QObject*>(QStringLiteral("probe")) : nullptr;
}

}  // namespace

TEST(ShellHostTest, AReloadMakesNewSingletons) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  writeSingletonShell(h);
  writeFoo(h, QStringLiteral("one"));
  h.makeHost();
  WarningCapture capture;
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_NE(probeOf(h), nullptr);
  EXPECT_EQ(probeOf(h)->property("count").toInt(), 1);

  // An old singleton would count 2; a new one starts again.
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_NE(probeOf(h), nullptr);
  EXPECT_EQ(probeOf(h)->property("count").toInt(), 1);
  EXPECT_FALSE(capture.has(QStringLiteral("Unable to assign")))
      << qPrintable(capture.lines.join('\n'));
}

TEST(ShellHostTest, EditingAComponentASingletonUsesAsAPropertyTypeReloadsCleanly) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  writeSingletonShell(h);
  writeFoo(h, QStringLiteral("one"));
  h.makeHost();
  WarningCapture capture;
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_NE(probeOf(h), nullptr);
  EXPECT_EQ(probeOf(h)->property("label").toString(), QStringLiteral("one"));

  write(h.dir.filePath(QStringLiteral("Foo.qml")),
        QStringLiteral("import QtQml\nQtObject { property string label: \"two\"; "
                       "property int extra: 1 }\n"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_NE(probeOf(h), nullptr);
  EXPECT_EQ(probeOf(h)->property("label").toString(), QStringLiteral("two"));
  EXPECT_FALSE(capture.has(QStringLiteral("Unable to assign")))
      << qPrintable(capture.lines.join('\n'));
  EXPECT_FALSE(capture.has(QStringLiteral("TypeError"))) << qPrintable(capture.lines.join('\n'));
}

TEST(ShellHostTest, ReloadTellsHoldersToResolveSingletonsAgain) {
  Harness h;
  h.start(shellConfig());
  QSignalSpy cleared(h.host.get(), &ShellHost::singletonsCleared);
  ASSERT_TRUE(h.host->reload());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(cleared.count(), 2);
  // A file that does not compile still resets the engine: the old shell is gone.
  write(h.shellFile(), QStringLiteral("not qml {{{"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_EQ(cleared.count(), 3);
}

namespace {

// A QML singleton that counts the instances alive.
class TrackedSingleton : public QObject {
 public:
  TrackedSingleton() { ++live; }
  ~TrackedSingleton() override { --live; }
  static inline int live = 0;
};

QObject* makeTrackedSingleton(QQmlEngine*, QJSEngine*) { return new TrackedSingleton; }

// The Quickshell Mpris pattern: a second name for the same singleton object.
QObject* makeWrappedSingleton(QQmlEngine* engine, QJSEngine*) {
  auto* tracked = engine->singletonInstance<QObject*>(QStringLiteral("waku.test.tracked"),
                                                      QStringLiteral("Tracked"));
  return tracked;
}

void registerTrackedSingleton() {
  static const int id = qmlRegisterSingletonType<TrackedSingleton>(
      "waku.test.tracked", 1, 0, "Tracked", &makeTrackedSingleton);
  static const int wrapped = qmlRegisterSingletonType<TrackedSingleton>(
      "waku.test.tracked", 1, 0, "Wrapped", &makeWrappedSingleton);
  (void)id;
  (void)wrapped;
}

}  // namespace

// The old singletons must be gone when the new shell is built: a service that owns a thread, a
// COM registration or an image provider must not run next to its replacement.
TEST(ShellHostTest, AReloadDestroysTheOldSingletonsBeforeTheNewShellIsBuilt) {
  registerTrackedSingleton();
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  write(h.shellFile(), QStringLiteral(R"(
import QtQuick
import waku
import waku.test.tracked
ShellRoot {
    QtObject { objectName: "probe"; property string name: Tracked.objectName + Wrapped.objectName }
    Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "x" } ] } }
}
)"));
  TrackedSingleton::live = 0;
  h.makeHost();
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  EXPECT_EQ(TrackedSingleton::live, 1);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
    EXPECT_EQ(TrackedSingleton::live, 1) << "reload " << i;
  }
}

// ---- the built-in default shell ----

namespace {

// A tiny stand-in for DefaultShell.qml, so the tests start none of the real services.
void useTinyDefaultShell(Harness& h) {
  const QString path = h.dir.filePath(QStringLiteral("default-shell.qml"));
  write(path, shellConfig(QStringLiteral("default")));
  h.defaultUrl = QUrl::fromLocalFile(path);
}

}  // namespace

TEST(ShellHostDefault, AnImplicitConfigHomeWithoutShellQmlShowsTheDefaultShellWithoutAnError) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinyDefaultShell(h);
  h.implicitConfigHome = true;
  h.makeHost();
  QSignalSpy errorChanged(h.host.get(), &ShellHost::errorChanged);
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
  EXPECT_EQ(errorChanged.count(), 0);
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("default"));
}

TEST(ShellHostDefault, AnExplicitConfigHomeWithoutShellQmlStaysAnError) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinyDefaultShell(h);
  h.implicitConfigHome = false;
  h.makeHost();
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("shell.qml not found")))
      << qPrintable(h.host->error());
  EXPECT_NE(h.text(QStringLiteral("a")), QStringLiteral("default"));  // the fallback shell
}

TEST(ShellHostDefault, AnImplicitConfigHomeWithShellQmlLoadsIt) {
  Harness h;
  useTinyDefaultShell(h);
  h.implicitConfigHome = true;
  h.start(shellConfig(QStringLiteral("mine")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("mine"));
}

TEST(ShellHostDefault, CreatingShellQmlSwitchesFromTheDefaultShell) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinyDefaultShell(h);
  h.implicitConfigHome = true;
  h.makeHost();
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("default"));
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);

  write(h.shellFile(), shellConfig(QStringLiteral("mine")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("mine"));
  EXPECT_TRUE(h.host->error().isEmpty());
}

TEST(ShellHostDefault, ACreatedConfigHomeFolderIsWatchedAfterItAppears) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  useTinyDefaultShell(h);
  const QString home = h.dir.filePath(QStringLiteral("missing/waku"));
  h.configHomeOverride = home;
  h.implicitConfigHome = true;
  h.makeHost();
  ASSERT_TRUE(h.host->reload());  // the folder does not exist: no crash, the default shell
  EXPECT_TRUE(h.host->error().isEmpty());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("default"));
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &ShellHost::reloaded);

  // Created in two steps, as a user would: the folder first, then the file.
  ASSERT_TRUE(QDir().mkpath(home));
  QTest::qWait(300);
  write(QDir(home).filePath(QStringLiteral("shell.qml")), shellConfig(QStringLiteral("mine")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("mine"));
}

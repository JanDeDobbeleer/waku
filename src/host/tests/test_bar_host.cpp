#include <gtest/gtest.h>

#include "host/testing/bar_fixtures.h"

// ---- Config and Bar declarations ----

TEST(BarConfig, ABarHasUsableDefaults) {
  QQmlEngine engine;
  QTemporaryDir dir;
  write(dir.filePath(QStringLiteral("bar.qml")),
        QStringLiteral("import wabar\nConfig { Bar {} }\n"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(dir.filePath(QStringLiteral("bar.qml"))));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  auto* config = qobject_cast<Config*>(root.get());
  ASSERT_NE(config, nullptr);
  ASSERT_EQ(config->barList().size(), 1);
  const Bar* bar = config->barList().first();
  EXPECT_EQ(bar->screens(), QStringList{QStringLiteral("primary")});
  EXPECT_EQ(bar->position(), Bar::Top);
  EXPECT_EQ(bar->height(), 40);
  EXPECT_FALSE(bar->reserveSpace());
  EXPECT_EQ(bar->backdrop(), Bar::None);
  EXPECT_TRUE(bar->enabled());
  EXPECT_EQ(bar->layout(), nullptr);
}

TEST(BarConfig, BarsAreReadInDeclarationOrderWithTheirProperties) {
  QQmlEngine engine;
  QTemporaryDir dir;
  write(dir.filePath(QStringLiteral("bar.qml")), QStringLiteral(R"(
import wabar
Config {
    Bar { height: 28; position: Bar.Bottom; backdrop: Bar.Mica; reserveSpace: true
          screens: ["all"]; background: "#112233"; enabled: false }
    Bar { screens: ["DISPLAY2", "primary"] }
}
)"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(dir.filePath(QStringLiteral("bar.qml"))));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  auto* config = qobject_cast<Config*>(root.get());
  ASSERT_NE(config, nullptr);
  ASSERT_EQ(config->barList().size(), 2);
  const Bar* first = config->barList()[0];
  EXPECT_EQ(first->height(), 28);
  EXPECT_EQ(first->position(), Bar::Bottom);
  EXPECT_EQ(first->backdrop(), Bar::Mica);
  EXPECT_TRUE(first->reserveSpace());
  EXPECT_EQ(first->screens(), QStringList{QStringLiteral("all")});
  EXPECT_EQ(first->background(), QColor(QStringLiteral("#112233")));
  EXPECT_FALSE(first->enabled());
  EXPECT_EQ(config->barList()[1]->screens(),
            (QStringList{QStringLiteral("DISPLAY2"), QStringLiteral("primary")}));
}

// ---- building windows ----

TEST(BarHostTest, BuildsOneWindowPerBarAndMatchingScreen) {
  Harness h;
  h.start(barConfig());
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_TRUE(h.host->error().isEmpty());
}

TEST(BarHostTest, AllScreensGetABar) {
  Harness h;
  h.screenCount = 2;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("screens: [\"all\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 2);
  EXPECT_NE(h.window(0), h.window(1));
}

TEST(BarHostTest, ABarForAnUnknownScreenOrDisabledBuildsNothing) {
  Harness h;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("screens: [\"DISPLAY9\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->windows().isEmpty());

  Harness d;
  d.start(barConfig(QStringLiteral("a"), QStringLiteral("enabled: false")));
  ASSERT_TRUE(d.host->reload());
  EXPECT_TRUE(d.host->windows().isEmpty());
}

TEST(BarHostTest, WindowSpansTheScreenAtTheDeclaredHeight) {
  Harness h;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("height: 32")));
  ASSERT_TRUE(h.host->reload());
  settle();
  const QScreen* screen = QGuiApplication::primaryScreen();
  EXPECT_EQ(h.window()->width(), screen->size().width());
  EXPECT_EQ(h.window()->height(), 32);
  EXPECT_EQ(h.window()->y(), screen->geometry().y());
}

TEST(BarHostTest, ABottomBarSitsAtTheBottomEdge) {
  Harness h;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("height: 32; position: Bar.Bottom")));
  ASSERT_TRUE(h.host->reload());
  settle();
  const QScreen* screen = QGuiApplication::primaryScreen();
  EXPECT_EQ(h.window()->y(), screen->geometry().y() + screen->geometry().height() - 32);
}

// ---- BarLayout ----

TEST(BarLayoutTest, WidgetsFlowStartCenterEnd) {
  Harness h;
  h.start(barConfig());
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

TEST(BarLayoutTest, AHiddenWidgetTakesNoSpace) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
Config {
    Bar {
        layout: BarLayout {
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

TEST(BarHostTest, AppliesTheBackdropAndSetsTheMonitorHandle) {
  Harness h;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("backdrop: Bar.Blur")));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.native.backdrops.size(), 1u);
  EXPECT_EQ(h.native.backdrops[0], Bar::Blur);
  EXPECT_EQ(h.window()->property("monitorHandle").toLongLong(), 4242);
}

TEST(BarHostTest, RegistersAnAppBarWithTheEdgeAndHeight) {
  Harness h;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("height: 30; position: Bar.Bottom")));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.native.created.size(), 1u);
  EXPECT_EQ(h.native.created[0].request.edge, wabar::host::AppBarEdge::Bottom);
  EXPECT_EQ(h.native.created[0].request.height, 30);
  EXPECT_TRUE(h.native.created[0].request.alwaysOnTop);
}

TEST(BarHostTest, ReservesSpaceOnlyWhenTheBarAsksAndTheHostAllows) {
  const struct {
    bool allow;
    bool ask;
    bool reserved;
  } cases[] = {
      {false, false, false}, {false, true, false}, {true, false, false}, {true, true, true}};
  for (const auto& c : cases) {
    Harness h;
    h.allowReserve = c.allow;
    h.start(barConfig(QStringLiteral("a"),
                      QStringLiteral("reserveSpace: ") + (c.ask ? "true" : "false")));
    ASSERT_TRUE(h.host->reload());
    ASSERT_EQ(h.native.created.size(), 1u);
    EXPECT_EQ(h.native.created[0].request.reserveSpace, c.reserved)
        << "allow=" << c.allow << " ask=" << c.ask;
  }
}

TEST(BarHostTest, DestroyingTheHostRemovesItsAppBars) {
  Harness h;
  h.screenCount = 2;
  h.start(barConfig(QStringLiteral("a"), QStringLiteral("screens: [\"all\"]")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.native.removed, 0);
  h.host.reset();
  EXPECT_EQ(h.native.removed, 2);
}

// ---- reload ----

TEST(BarHostTest, ReloadReplacesTheWindowsWithTheNewConfig) {
  Harness h;
  h.start(barConfig(QStringLiteral("first")));
  ASSERT_TRUE(h.host->reload());
  QPointer<QQuickWindow> old = h.window();
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("first"));

  write(h.barFile(), barConfig(QStringLiteral("second")));
  QSignalSpy reloaded(h.host.get(), &BarHost::reloaded);
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(reloaded.count(), 1);
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("second"));
  QTest::qWait(10);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_TRUE(old.isNull());
  EXPECT_EQ(h.native.removed, 1);  // the old window's app bar went with it
}

TEST(BarHostTest, ABrokenReloadKeepsThePreviousBarsAndReportsTheError) {
  Harness h;
  h.start(barConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  QQuickWindow* before = h.window();
  QSignalSpy errorChanged(h.host.get(), &BarHost::errorChanged);

  write(h.barFile(), barConfig(QStringLiteral("x")) + QStringLiteral("\nthis is not qml {{{"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.window(), before);  // the very same window
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("good"));
  EXPECT_EQ(errorChanged.count(), 1);
  EXPECT_TRUE(h.host->error().startsWith(QStringLiteral("bar.qml:")))
      << qPrintable(h.host->error());

  // The pill is visible in the kept bar.
  settle();
  QQuickItem* pill = findItem(before, QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

TEST(BarHostTest, FixingTheConfigClearsTheError) {
  Harness h;
  h.start(barConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.barFile(), QStringLiteral("not qml {{{"));
  ASSERT_FALSE(h.host->reload());
  ASSERT_FALSE(h.host->error().isEmpty());

  write(h.barFile(), barConfig(QStringLiteral("fixed")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("fixed"));
  settle();
  EXPECT_FALSE(findItem(h.window(), QStringLiteral("errorPill"))->isVisible());
}

TEST(BarHostTest, ARootThatIsNotAConfigIsAnError) {
  Harness h;
  h.start(barConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.barFile(), QStringLiteral("import QtQuick\nItem {}\n"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("must declare a Config")))
      << qPrintable(h.host->error());
  EXPECT_EQ(h.host->windows().size(), 1);  // still the old bar
}

TEST(BarHostTest, AnUnknownWidgetTypeIsAnError) {
  Harness h;
  h.start(barConfig(QStringLiteral("good")));
  ASSERT_TRUE(h.host->reload());
  write(h.barFile(), QStringLiteral(R"(
import wabar
Config { Bar { layout: BarLayout { start: [ NoSuchWidget {} ] } } }
)"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("good"));
}

TEST(BarHostTest, ABrokenFirstLoadShowsAFallbackBarWithTheError) {
  Harness h;
  h.start(QStringLiteral("not qml {{{"));
  EXPECT_FALSE(h.host->reload());
  EXPECT_FALSE(h.host->error().isEmpty());
  ASSERT_EQ(h.host->windows().size(), 1);  // the fallback bar
  settle();
  QQuickItem* pill = findItem(h.window(), QStringLiteral("errorPill"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->isVisible());
}

TEST(BarHostTest, AMissingConfigFileIsAnErrorWithAFallbackBar) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  h.makeHost();  // no bar.qml was written
  EXPECT_FALSE(h.host->reload());
  EXPECT_TRUE(h.host->error().contains(QStringLiteral("bar.qml not found")))
      << qPrintable(h.host->error());
  EXPECT_EQ(h.host->windows().size(), 1);
}

TEST(BarHostTest, ChangingAFileInTheConfigHomeReloads) {
  Harness h;
  h.start(barConfig(QStringLiteral("before")));
  ASSERT_TRUE(h.host->reload());
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &BarHost::reloaded);

  write(h.barFile(), barConfig(QStringLiteral("after")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("after"));
}

TEST(BarHostTest, ChangingAWidgetFileInASubfolderReloads) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  QDir(h.dir.path()).mkdir(QStringLiteral("widgets"));
  const QString widget = h.dir.filePath(QStringLiteral("widgets/Label.qml"));
  write(widget, QStringLiteral("import QtQuick\nText { objectName: \"w\"; text: \"first\" }\n"));
  h.start(QStringLiteral(R"(
import wabar
import "widgets"
Config { Bar { layout: BarLayout { start: [ Label {} ] } } }
)"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.text(QStringLiteral("w")), QStringLiteral("first"));
  h.host->watch();
  QSignalSpy reloaded(h.host.get(), &BarHost::reloaded);

  write(widget, QStringLiteral("import QtQuick\nText { objectName: \"w\"; text: \"second\" }\n"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return reloaded.count() >= 1; }, 5000));
  EXPECT_TRUE(h.host->error().isEmpty()) << qPrintable(h.host->error());
  EXPECT_EQ(h.text(QStringLiteral("w")), QStringLiteral("second"));
}

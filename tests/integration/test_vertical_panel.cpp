// Panels docked to the left and right edge: the layout runs top to bottom, widgets use Strip and
// the vertical Pill, popups open sideways, and examples/sidebar loads. Offscreen, on one screen;
// no app bar is registered (the fake records the request).
#include <gtest/gtest.h>

#include <QFile>

#include "host/testing/widget_fixtures.h"

namespace {

QString sidePanel(const QString& edge, const QString& layout, const QString& panelProperties = {}) {
  return QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
ShellRoot {
    Panel {
        edge: Panel.%1
        thickness: 56
        %3
        content: PanelLayout {
            objectName: "layout"
            %2
        }
    }
}
)")
      .arg(edge, layout, panelProperties);
}

const char* kThreeBoxes =
    R"(start: [ Rectangle { objectName: "a"; width: 30; height: 20 } ]
       center: [ Rectangle { objectName: "b"; width: 30; height: 20 } ]
       end: [ Rectangle { objectName: "c"; width: 30; height: 20 } ])";

QPointF inWindow(Harness& h, const QString& name) {
  QQuickItem* i = item(h, name);
  return i ? i->mapToItem(h.window()->contentItem(), QPointF()) : QPointF(-1, -1);
}

QQuickItem* visibleText(QQuickItem* root, const QString& text) {
  for (QQuickItem* child : root->childItems()) {
    if (child->isVisible() && child->property("text").toString() == text &&
        child->property("elide").isValid())
      return child;
    if (QQuickItem* found = visibleText(child, text)) return found;
  }
  return nullptr;
}

QRect screenRect() { return QGuiApplication::primaryScreen()->geometry(); }

}  // namespace

// ---- the window ----

TEST(VerticalPanel, ALeftPanelHugsTheLeftEdgeAndFillsTheHeight) {
  Harness h;
  h.start(sidePanel(QStringLiteral("Left"), QString::fromLatin1(kThreeBoxes)));
  ASSERT_TRUE(started(h));
  settle();
  EXPECT_EQ(h.window()->geometry(),
            QRect(screenRect().x(), screenRect().y(), 56, screenRect().height()));
  EXPECT_EQ(h.shellWindow()->property("dockEdge").toInt(), DockEdge::Left);
  EXPECT_TRUE(h.shellWindow()->property("dockVertical").toBool());
}

TEST(VerticalPanel, ARightPanelHugsTheRightEdge) {
  Harness h;
  h.start(sidePanel(QStringLiteral("Right"), QString::fromLatin1(kThreeBoxes)));
  ASSERT_TRUE(started(h));
  settle();
  const QRect s = screenRect();
  EXPECT_EQ(h.window()->geometry(), QRect(s.x() + s.width() - 56, s.y(), 56, s.height()));
  EXPECT_EQ(h.shellWindow()->property("dockEdge").toInt(), DockEdge::Right);
}

// ---- PanelLayout ----

TEST(VerticalPanel, TheGroupsRunTopToBottomAndCentreAcrossThePanel) {
  Harness h;
  h.start(sidePanel(QStringLiteral("Left"), QString::fromLatin1(kThreeBoxes)));
  ASSERT_TRUE(started(h));
  settle();
  const QPointF a = inWindow(h, QStringLiteral("a"));
  const QPointF b = inWindow(h, QStringLiteral("b"));
  const QPointF c = inWindow(h, QStringLiteral("c"));
  const double height = h.window()->height();
  EXPECT_DOUBLE_EQ(a.y(), 12);                // startPadding
  EXPECT_DOUBLE_EQ(c.y() + 20, height - 12);  // endPadding
  EXPECT_DOUBLE_EQ(b.y(), (height - 20) / 2);
  EXPECT_LT(a.y(), b.y());
  EXPECT_LT(b.y(), c.y());
  for (double x : {a.x(), b.x(), c.x()}) EXPECT_DOUBLE_EQ(x, (56 - 30) / 2.0);
}

TEST(VerticalPanel, AHorizontalPanelStillRunsLeftToRight) {
  Harness h;
  h.start(sidePanel(QStringLiteral("Top"), QString::fromLatin1(kThreeBoxes)));
  ASSERT_TRUE(started(h));
  settle();
  const QPointF a = inWindow(h, QStringLiteral("a"));
  const QPointF c = inWindow(h, QStringLiteral("c"));
  EXPECT_DOUBLE_EQ(a.x(), 12);
  EXPECT_DOUBLE_EQ(c.x() + 30, h.window()->width() - 12);
  EXPECT_DOUBLE_EQ(a.y(), (56 - 20) / 2.0);
}

TEST(VerticalPanel, IslandsMeetTheScreenEdgeOnTheirOwnSide) {
  const struct {
    const char* edge;
    bool flushLeft;
  } cases[] = {{"Left", true}, {"Right", false}};
  for (const auto& c : cases) {
    Harness h;
    write(h.dir.filePath(QStringLiteral("theme.qml")),
          QStringLiteral(
              "import wabar\nThemeSpec { islands: true; groupPadding: 10; islandRadius: 8 }\n"));
    h.start(sidePanel(QString::fromLatin1(c.edge), QString::fromLatin1(kThreeBoxes)));
    ASSERT_TRUE(started(h)) << c.edge;
    settle();
    QQuickItem* island = item(h, QStringLiteral("island-start"));
    ASSERT_NE(island, nullptr);
    EXPECT_TRUE(island->isVisible());
    // The island spans the panel's thickness and the group's length plus its padding.
    EXPECT_DOUBLE_EQ(island->width(), 56);
    EXPECT_DOUBLE_EQ(island->height(), 20 + 20);
    EXPECT_DOUBLE_EQ(island->mapToItem(h.window()->contentItem(), QPointF()).x(), 0);
    EXPECT_DOUBLE_EQ(island->mapToItem(h.window()->contentItem(), QPointF()).y(),
                     inWindow(h, QStringLiteral("a")).y() - 10);
    // The shape's local frame is mapped onto the panel: the screen edge is x = 0 on the left,
    // x = width on the right.
    QQuickItem* shape = nullptr;
    for (QQuickItem* child : island->childItems()) {
      for (QQuickItem* grandchild : child->childItems()) {
        for (QQuickItem* leaf : grandchild->childItems()) {
          if (leaf->property("preferredRendererType").isValid()) shape = leaf;
        }
      }
    }
    ASSERT_NE(shape, nullptr) << c.edge;
    const QPointF depthOrigin = shape->mapToItem(island, QPointF(0, 0));
    const QPointF depthEnd = shape->mapToItem(island, QPointF(0, 56));  // depth 0 .. thickness
    EXPECT_DOUBLE_EQ(depthOrigin.x(), c.flushLeft ? 0 : 56) << c.edge;
    EXPECT_DOUBLE_EQ(depthEnd.x(), c.flushLeft ? 56 : 0) << c.edge;
  }
}

// ---- Strip ----

TEST(VerticalPanel, AStripStacksItsItemsAlongThePanel) {
  const char* strip = R"(start: [ Strip {
      objectName: "strip"
      spacing: 4
      Rectangle { objectName: "s1"; width: 20; height: 10 }
      Rectangle { objectName: "s2"; width: 20; height: 10 }
      Rectangle { objectName: "s3"; width: 20; height: 10 }
    } ])";
  {
    Harness h;
    h.start(sidePanel(QStringLiteral("Left"), QString::fromLatin1(strip)));
    ASSERT_TRUE(started(h));
    settle();
    EXPECT_TRUE(item(h, QStringLiteral("strip"))->property("vertical").toBool());
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("s2"))->y() - item(h, QStringLiteral("s1"))->y(), 14);
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("s3"))->y() - item(h, QStringLiteral("s2"))->y(), 14);
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("s2"))->x(), 0);
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("strip"))->height(), 38);
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("strip"))->width(), 20);
  }
  {
    Harness h;
    h.start(sidePanel(QStringLiteral("Top"), QString::fromLatin1(strip)));
    ASSERT_TRUE(started(h));
    settle();
    EXPECT_FALSE(item(h, QStringLiteral("strip"))->property("vertical").toBool());
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("s2"))->x() - item(h, QStringLiteral("s1"))->x(), 24);
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("s2"))->y(), 0);
    EXPECT_DOUBLE_EQ(item(h, QStringLiteral("strip"))->width(), 68);
  }
}

// ---- Pill ----

TEST(VerticalPanel, APillShowsItsIconAboveItsTextAndFitsThePanel) {
  Harness h;
  h.start(sidePanel(
      QStringLiteral("Left"),
      QStringLiteral(R"(start: [ Pill { objectName: "p"; icon: "#"; text: "14:32" } ])")));
  ASSERT_TRUE(started(h));
  settle();
  QQuickItem* pill = item(h, QStringLiteral("p"));
  ASSERT_NE(pill, nullptr);
  EXPECT_TRUE(pill->property("vertical").toBool());
  EXPECT_GE(pill->width(), 24);  // Theme.pillHeight
  EXPECT_LE(pill->width(), 56);
  QQuickItem* icon = visibleText(pill, QStringLiteral("#"));
  QQuickItem* label = visibleText(pill, QStringLiteral("14:32"));
  ASSERT_TRUE(icon && label);
  EXPECT_LT(icon->mapToItem(pill, QPointF()).y(), label->mapToItem(pill, QPointF()).y());
  EXPECT_DOUBLE_EQ(label->rotation(), 0);  // the text stays horizontal
  EXPECT_GT(pill->height(), 24);           // taller than a horizontal pill
}

TEST(VerticalPanel, AHorizontalPillIsUnchanged) {
  Harness h;
  h.start(sidePanel(
      QStringLiteral("Top"),
      QStringLiteral(R"(start: [ Pill { objectName: "p"; icon: "#"; text: "14:32" } ])")));
  ASSERT_TRUE(started(h));
  settle();
  QQuickItem* pill = item(h, QStringLiteral("p"));
  EXPECT_FALSE(pill->property("vertical").toBool());
  EXPECT_DOUBLE_EQ(pill->height(), 24);
  QQuickItem* icon = visibleText(pill, QStringLiteral("#"));
  QQuickItem* label = visibleText(pill, QStringLiteral("14:32"));
  ASSERT_TRUE(icon && label);
  EXPECT_LT(icon->mapToItem(pill, QPointF()).x(), label->mapToItem(pill, QPointF()).x());
}

TEST(VerticalPanel, ALongLabelIsElidedToThePanelThickness) {
  Harness h;
  h.start(sidePanel(
      QStringLiteral("Left"),
      QStringLiteral(R"(start: [ Pill { objectName: "p"; text: "a very long label indeed" } ])")));
  ASSERT_TRUE(started(h));
  settle();
  QQuickItem* pill = item(h, QStringLiteral("p"));
  EXPECT_LE(pill->width(), 56);
  QQuickItem* label = visibleText(pill, QStringLiteral("a very long label indeed"));
  ASSERT_NE(label, nullptr);
  EXPECT_LT(label->width(), label->implicitWidth());
  EXPECT_LE(label->width(), 56);
}

TEST(VerticalPanel, RotatedTextIsAnOptIn) {
  Harness h;
  h.start(sidePanel(
      QStringLiteral("Left"),
      QStringLiteral(R"(start: [ Pill { objectName: "p"; text: "volume"; rotateText: true } ])")));
  ASSERT_TRUE(started(h));
  settle();
  QQuickItem* pill = item(h, QStringLiteral("p"));
  QQuickItem* label = visibleText(pill, QStringLiteral("volume"));
  ASSERT_NE(label, nullptr);
  EXPECT_DOUBLE_EQ(label->rotation(), 90);
  EXPECT_LT(pill->width(), pill->height());  // the label runs along the panel
}

// ---- popups ----

TEST(VerticalPanel, APopupOpensBesideALeftPanelAndALeftOfARightOne) {
  const char* layout = R"(start: [ Item {
      id: box
      objectName: "box"
      width: 40
      height: 24
      PopupWindow {
          objectName: "pop"
          anchor.item: box
          Text { objectName: "content"; text: "hello"; color: "white" }
      }
    } ])";
  {
    Harness h;
    h.start(sidePanel(QStringLiteral("Left"), QString::fromLatin1(layout)));
    ASSERT_TRUE(started(h));
    QQuickWindow* popup = findWindow(QStringLiteral("pop"));
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
    // The platform reports the new geometry asynchronously.
    EXPECT_TRUE(
        QTest::qWaitFor([&] { return popup->x() >= h.window()->x() + h.window()->width(); }, 2000));
    EXPECT_GE(popup->x(), h.window()->x() + h.window()->width());  // beside the panel
    const int boxY = qRound(item(h, QStringLiteral("box"))->mapToGlobal(QPointF(0, 0)).y());
    EXPECT_LE(popup->y(), boxY + 24);
  }
  {
    Harness h;
    h.start(sidePanel(QStringLiteral("Right"), QString::fromLatin1(layout)));
    ASSERT_TRUE(started(h));
    QQuickWindow* popup = findWindow(QStringLiteral("pop"));
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
    EXPECT_TRUE(QTest::qWaitFor(
        [&] { return popup->x() > 0 && popup->x() + popup->width() <= h.window()->x(); }, 2000));
    EXPECT_LE(popup->x() + popup->width(), h.window()->x());
  }
}

// ---- the app bar ----

TEST(VerticalPanel, ALeftPanelRegistersALeftAppBarOfItsWidth) {
  Harness h;
  h.allowReserve = true;
  h.start(sidePanel(QStringLiteral("Left"), QString::fromLatin1(kThreeBoxes),
                    QStringLiteral("exclusive: true")));
  ASSERT_TRUE(started(h));
  ASSERT_EQ(h.native.created.size(), 1u);
  EXPECT_EQ(h.native.created[0].request.edge, wabar::host::AppBarEdge::Left);
  EXPECT_EQ(h.native.created[0].request.thickness, 56);
  EXPECT_TRUE(h.native.created[0].request.reserveSpace);
}

// ---- the sample ----

namespace {

QString sidebarExample() {
  QFile file(QStringLiteral(WABAR_EXAMPLES_DIR "/sidebar/shell.qml"));
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(file.readAll());
}

}  // namespace

TEST(SidebarExample, LoadsAsALeftDockedVerticalPanelOnEveryScreen) {
  const QString shell = sidebarExample();
  ASSERT_FALSE(shell.isEmpty());
  Harness h;
  h.screenCount = 2;
  h.allowReserve = true;
  h.start(shell);
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty()) << h.host->error().toStdString();
  ASSERT_EQ(h.host->windows().size(), 2);
  ASSERT_TRUE(QTest::qWaitForWindowExposed(h.window()));
  settle();

  const QRect s = screenRect();
  EXPECT_EQ(h.window()->geometry(), QRect(s.x(), s.y(), 56, s.height()));
  EXPECT_EQ(h.shellWindow()->property("dockEdge").toInt(), DockEdge::Left);
  EXPECT_TRUE(h.shellWindow()->property("dockVertical").toBool());

  ASSERT_EQ(h.native.created.size(), 2u);
  EXPECT_EQ(h.native.created[0].request.edge, wabar::host::AppBarEdge::Left);
  EXPECT_EQ(h.native.created[0].request.thickness, 56);
  EXPECT_TRUE(h.native.created[0].request.reserveSpace);

  // The clock is the first widget at the top; its pill is the vertical one and fits the panel.
  QQuickItem* clock = item(h, QStringLiteral("clock"));
  ASSERT_NE(clock, nullptr);
  EXPECT_TRUE(clock->property("vertical").toBool());
  EXPECT_LE(clock->width(), 56);
  EXPECT_LT(inWindow(h, QStringLiteral("clock")).y(), 40);
}

#include <gtest/gtest.h>

#include "host/qml/popup_placement_qml.h"
#include "host/testing/shell_fixtures.h"

namespace {

QString popupConfig(const QString& barProperties = QString(),
                    const QString& group = QStringLiteral("start")) {
  return QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Panel {
        %1
        content: PanelLayout {
            %2: [
                Item {
                    id: anchorBox
                    objectName: "anchor"
                    width: 60
                    height: 24
                    PopupWindow {
                        objectName: "pop"
                        anchor.item: anchorBox
                        Text { objectName: "content"; text: "hello"; color: "white"; font.pixelSize: 14 }
                    }
                }
            ]
        }
    }
}
)")
      .arg(barProperties, group);
}

QString tooltipConfig(const QString& text = QStringLiteral("hello tip")) {
  return QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [
                Rectangle {
                    objectName: "target"
                    width: 80
                    height: 24
                    color: "gray"
                    Tooltip { objectName: "tip"; text: "%1"; delay: 80 }
                }
            ]
        }
    }
}
)")
      .arg(text);
}

QPoint centerOf(QQuickWindow* window, const QString& name) {
  QQuickItem* item = findItem(window, name);
  return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

// Qt drops a pointer move that does not change the position, so go somewhere else first.
void hoverOver(Harness& h, const QString& name) {
  QTest::mouseMove(h.window(), QPoint(h.window()->width() - 5, h.window()->height() - 3));
  QTest::mouseMove(h.window(), centerOf(h.window(), name));
}

// The Timer behind a Tooltip's `delay`: running means the tip shows when it fires.
bool tooltipPending(Harness& h, const QString& name) {
  QQuickItem* tooltip = findItem(h.window(), name);
  const QObject* timer =
      tooltip ? tooltip->findChild<QObject*>(QStringLiteral("tooltipDelay")) : nullptr;
  return timer && timer->property("running").toBool();
}

bool started(Harness& h) {
  if (!h.host->reload()) return false;
  return h.window() && QTest::qWaitForWindowExposed(h.window());
}

void invoke(QObject* object, const char* method) {
  ASSERT_TRUE(QMetaObject::invokeMethod(object, method));
}

// A Tooltip creates its window on the first show, so wait for it after the hover.
QQuickWindow* waitForTooltipWindow() {
  QQuickWindow* tip = nullptr;
  if (!QTest::qWaitFor(
          [&] {
            tip = findWindow(QStringLiteral("tooltipWindow"));
            return tip != nullptr;
          },
          2000)) {
    return nullptr;
  }
  return tip;
}

const QRectF kScreen(0, 0, 1000, 800);
const QRectF kAnchor(400, 0, 100, 40);
const QSizeF kSize(200, 100);

}  // namespace

// ---- placement ----

TEST(PopupPlacementTest, CentresBelowTheAnchorByDefault) {
  const QPointF p = PopupPlacement::place(kAnchor, kSize, kScreen, PopupPlacement::Below, 6, 8);
  EXPECT_DOUBLE_EQ(p.x(), 350);  // anchor centre 450, minus half the width
  EXPECT_DOUBLE_EQ(p.y(), 46);   // anchor bottom 40 + gap
}

TEST(PopupPlacementTest, SlidesSidewaysToStayOnScreen) {
  const QPointF left =
      PopupPlacement::place(QRectF(0, 0, 40, 40), kSize, kScreen, PopupPlacement::Below, 6, 8);
  EXPECT_DOUBLE_EQ(left.x(), 8);
  const QPointF right =
      PopupPlacement::place(QRectF(960, 0, 40, 40), kSize, kScreen, PopupPlacement::Below, 6, 8);
  EXPECT_DOUBLE_EQ(right.x(), 1000 - 200 - 8);
}

TEST(PopupPlacementTest, AbovePlacesTheWholePopupAboveTheAnchor) {
  const QPointF p =
      PopupPlacement::place(QRectF(400, 760, 100, 40), kSize, kScreen, PopupPlacement::Above, 6, 8);
  EXPECT_DOUBLE_EQ(p.y(), 760 - 6 - 100);
}

TEST(PopupPlacementTest, FlipsWhenThePreferredSideHasNoRoom) {
  // Below the anchor there is no room; above there is.
  const QPointF flippedUp =
      PopupPlacement::place(QRectF(400, 760, 100, 40), kSize, kScreen, PopupPlacement::Below, 6, 8);
  EXPECT_DOUBLE_EQ(flippedUp.y(), 760 - 6 - 100);
  // Above the anchor there is no room; below there is.
  const QPointF flippedDown =
      PopupPlacement::place(kAnchor, kSize, kScreen, PopupPlacement::Above, 6, 8);
  EXPECT_DOUBLE_EQ(flippedDown.y(), 46);
}

TEST(PopupPlacementTest, ClampsWhenNeitherSideFits) {
  const QSizeF tall(200, 700);
  const QPointF p =
      PopupPlacement::place(QRectF(400, 380, 100, 40), tall, kScreen, PopupPlacement::Below, 6, 8);
  EXPECT_GE(p.y(), 8);
  EXPECT_LE(p.y() + tall.height(), 800 - 8 + 1e-9);
}

TEST(PopupPlacementTest, ARoomTooSmallKeepsTheTopLeftVisible) {
  const QPointF p =
      PopupPlacement::place(kAnchor, QSizeF(2000, 2000), kScreen, PopupPlacement::Below, 6, 8);
  EXPECT_DOUBLE_EQ(p.x(), 8);
  EXPECT_DOUBLE_EQ(p.y(), 8);
}

TEST(PopupPlacementTest, GapAndMarginAreHonoured) {
  const QPointF p = PopupPlacement::place(kAnchor, kSize, kScreen, PopupPlacement::Below, 20, 8);
  EXPECT_DOUBLE_EQ(p.y(), 60);
  const QPointF edge =
      PopupPlacement::place(QRectF(0, 0, 10, 40), kSize, kScreen, PopupPlacement::Below, 6, 30);
  EXPECT_DOUBLE_EQ(edge.x(), 30);
}

// A vertical panel: the popup opens to the side and slides along the anchor.
TEST(PopupPlacementTest, RightOpensBesideTheAnchorAndCentresVertically) {
  const QRectF anchor(0, 300, 48, 40);
  const QPointF p = PopupPlacement::place(anchor, kSize, kScreen, PopupPlacement::Right, 6, 8);
  EXPECT_DOUBLE_EQ(p.x(), 54);        // anchor right 48 + gap
  EXPECT_DOUBLE_EQ(p.y(), 320 - 50);  // anchor centre 320, minus half the height
}

TEST(PopupPlacementTest, LeftOpensBesideTheAnchor) {
  const QRectF anchor(952, 300, 48, 40);
  const QPointF p = PopupPlacement::place(anchor, kSize, kScreen, PopupPlacement::Left, 6, 8);
  EXPECT_DOUBLE_EQ(p.x(), 952 - 6 - 200);
  EXPECT_DOUBLE_EQ(p.y(), 270);
}

TEST(PopupPlacementTest, SideEdgesSlideAlongTheScreenEdgeAndFlip) {
  const QPointF top =
      PopupPlacement::place(QRectF(0, 0, 48, 40), kSize, kScreen, PopupPlacement::Right, 6, 8);
  EXPECT_DOUBLE_EQ(top.y(), 8);
  const QPointF bottom =
      PopupPlacement::place(QRectF(0, 760, 48, 40), kSize, kScreen, PopupPlacement::Right, 6, 8);
  EXPECT_DOUBLE_EQ(bottom.y(), 800 - 100 - 8);
  // No room on the right of an anchor at the right edge: flips to its left.
  const QPointF flipped =
      PopupPlacement::place(QRectF(952, 300, 48, 40), kSize, kScreen, PopupPlacement::Right, 6, 8);
  EXPECT_DOUBLE_EQ(flipped.x(), 952 - 6 - 200);
  // No room on the left of an anchor at the left edge: flips to its right.
  const QPointF flippedRight =
      PopupPlacement::place(QRectF(0, 300, 48, 40), kSize, kScreen, PopupPlacement::Left, 6, 8);
  EXPECT_DOUBLE_EQ(flippedRight.x(), 54);
}

TEST(PopupPlacementTest, EachDockEdgeOpensAwayFromItsEdge) {
  using waku::host::AppBarEdge;
  using waku::host::PopupEdge;
  using waku::host::popupEdgeForDock;
  EXPECT_EQ(popupEdgeForDock(AppBarEdge::Top), PopupEdge::Below);
  EXPECT_EQ(popupEdgeForDock(AppBarEdge::Bottom), PopupEdge::Above);
  EXPECT_EQ(popupEdgeForDock(AppBarEdge::Left), PopupEdge::Right);
  EXPECT_EQ(popupEdgeForDock(AppBarEdge::Right), PopupEdge::Left);
  EXPECT_EQ(popupEdgeForDock(std::nullopt), PopupEdge::Below);
}

TEST(PopupPlacementTest, TheAnchorIsExtendedAcrossThePanelThickness) {
  using waku::host::panelAnchorRect;
  const QRectF item(200, 4, 60, 24);
  EXPECT_EQ(panelAnchorRect(item, QRectF(0, 0, 1000, 40), false), QRectF(200, 0, 60, 40));
  const QRectF sideItem(4, 200, 40, 24);
  EXPECT_EQ(panelAnchorRect(sideItem, QRectF(0, 0, 48, 800), true), QRectF(0, 200, 48, 24));
}

TEST(PopupPlacementTest, TheQmlEdgeForADockMatches) {
  PopupPlacement qml;
  EXPECT_EQ(qml.edgeForDock(waku::host::DockEdge::Top), PopupPlacement::Below);
  EXPECT_EQ(qml.edgeForDock(waku::host::DockEdge::Bottom), PopupPlacement::Above);
  EXPECT_EQ(qml.edgeForDock(waku::host::DockEdge::Left), PopupPlacement::Right);
  EXPECT_EQ(qml.edgeForDock(waku::host::DockEdge::Right), PopupPlacement::Left);
  EXPECT_EQ(qml.edgeForDock(waku::host::DockEdge::None), PopupPlacement::Below);
}

// ---- PopupWindow ----

TEST(PopupWindowTest, StartsClosed) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  ASSERT_NE(popup, nullptr);
  EXPECT_FALSE(popup->isVisible());
}

TEST(PopupWindowTest, OpensBelowTheBarAndSizesToItsContent) {
  Harness h;
  h.start(popupConfig(QStringLiteral("thickness: 40")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  QSignalSpy opened(popup, SIGNAL(opened()));
  invoke(popup, "open");
  EXPECT_TRUE(popup->isVisible());
  EXPECT_EQ(opened.count(), 1);
  QQuickItem* content = findItem(popup, QStringLiteral("content"));
  ASSERT_NE(content, nullptr);
  // The platform reports the new geometry asynchronously.
  EXPECT_TRUE(QTest::qWaitFor(
      [&] { return popup->y() >= 40 && popup->width() == qCeil(content->implicitWidth()) + 20; },
      2000));

  EXPECT_GE(popup->y(), 40);                                        // below the bar
  EXPECT_EQ(popup->width(), qCeil(content->implicitWidth()) + 20);  // the default padding
  EXPECT_EQ(popup->height(), qCeil(content->implicitHeight()) + 20);
  const QRect screen = QGuiApplication::primaryScreen()->geometry();
  EXPECT_GE(popup->x(), screen.x() + 8);
  EXPECT_LE(popup->x() + popup->width(), screen.right() - 8 + 1);
}

TEST(PopupWindowTest, ABottomBarOpensItAboveTheWidget) {
  Harness h;
  h.start(popupConfig(QStringLiteral("thickness: 40; edge: Panel.Bottom")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return popup->y() + popup->height() <= h.window()->y(); }, 2000));
}

TEST(PopupWindowTest, AWidgetAtTheScreenEdgeKeepsItOnScreen) {
  Harness h;
  h.start(popupConfig(QString(), QStringLiteral("end")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  const QRect screen = QGuiApplication::primaryScreen()->geometry();
  EXPECT_TRUE(QTest::qWaitFor(
      [&] { return popup->x() > 0 && popup->x() + popup->width() <= screen.right() - 8 + 1; },
      2000));
}

TEST(PopupWindowTest, ContentGrowingAfterOpenStaysOnScreen) {
  Harness h;
  h.start(popupConfig(QString(), QStringLiteral("end")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  // Content wider than the room to the right of the anchor, arriving after the window is shown.
  QQuickItem* content = findItem(popup, QStringLiteral("content"));
  ASSERT_NE(content, nullptr);
  content->setProperty("width", 700);
  const QRect screen = QGuiApplication::primaryScreen()->geometry();
  EXPECT_TRUE(QTest::qWaitFor(
      [&] {
        return popup->width() >= 700 && popup->x() >= screen.x() + 8 &&
               popup->x() + popup->width() <= screen.right() - 8 + 1;
      },
      2000));
  EXPECT_GE(popup->width(), 700);
  EXPECT_GE(popup->x(), screen.x() + 8);
  EXPECT_LE(popup->x() + popup->width(), screen.right() - 8 + 1);
}

TEST(PopupWindowTest, UpdateAnchorPlacesAnOpenPopupAgain) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  QObject* anchor = popup->property("anchor").value<QObject*>();
  ASSERT_NE(anchor, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->y() >= 0 && popup->isVisible(); }, 2000));
  const int before = popup->x();
  // A rect change alone does not move the popup; updateAnchor() does.
  anchor->setProperty("rect", QRectF(300, 0, 60, 24));
  invoke(anchor, "updateAnchor");
  EXPECT_TRUE(QTest::qWaitFor([&] { return popup->x() >= before + 250; }, 2000));
}

TEST(PopupWindowTest, CloseHidesItAndSignalsOnce) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  QSignalSpy closed(popup, SIGNAL(closed()));
  invoke(popup, "open");
  invoke(popup, "close");
  EXPECT_FALSE(popup->isVisible());
  EXPECT_EQ(closed.count(), 1);
}

TEST(PopupWindowTest, ToggleOpensAndClosesButNotInTheSameBreath) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "toggle");
  EXPECT_TRUE(popup->isVisible());
  invoke(popup, "toggle");
  EXPECT_FALSE(popup->isVisible());
  // The press that closed a popup must not reopen it through the widget's click handler.
  invoke(popup, "toggle");
  EXPECT_FALSE(popup->isVisible());
  // The guard is 250 ms from `closedAt`: move the close back in time instead of waiting it out.
  popup->setProperty("closedAt", popup->property("closedAt").toDouble() - 300);
  invoke(popup, "toggle");
  EXPECT_TRUE(popup->isVisible());
}

TEST(PopupWindowTest, EscapeClosesIt) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  ASSERT_TRUE(QTest::qWaitForWindowExposed(popup));
  popup->requestActivate();
  QTest::keyClick(popup, Qt::Key_Escape);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !popup->isVisible(); }, 2000));
}

TEST(PopupWindowTest, APressInTheBarClosesIt) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  ASSERT_TRUE(popup->isVisible());
  const QPoint away(h.window()->width() - 20, 10);
  QTest::mousePress(h.window(), Qt::LeftButton, Qt::NoModifier, away);
  EXPECT_FALSE(popup->isVisible());
  QTest::mouseRelease(h.window(), Qt::LeftButton, Qt::NoModifier, away);
}

TEST(PopupWindowTest, LosingActivationClosesIt) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  ASSERT_TRUE(QTest::qWaitForWindowActive(popup));
  // The popup is a transient child of the bar, so activating the bar changes nothing. The user
  // going to another application means an unrelated window gets the focus.
  QQuickWindow other;
  other.resize(100, 100);
  other.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&other));
  other.requestActivate();
  EXPECT_TRUE(QTest::qWaitFor([&] { return !popup->isVisible(); }, 2000));
}

TEST(PopupWindowTest, ABarGainingActivationDoesNothingToAClosedPopup) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  h.window()->requestActivate();
  // The platform posts the activation as a window system event; one pass delivers it.
  QCoreApplication::processEvents();
  EXPECT_FALSE(popup->isVisible());
}

TEST(PopupWindowTest, ItGetsTheBackdropOfItsBar) {
  Harness h;
  h.start(popupConfig(QStringLiteral("backdrop: Backdrop.Blur")));
  ASSERT_TRUE(started(h));
  const size_t before = h.native.backdrops.size();
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  ASSERT_EQ(h.native.backdrops.size(), before + 1);
  EXPECT_EQ(h.native.backdrops.back(), Backdrop::Blur);
}

TEST(PopupWindowTest, ChromeFollowsTheThemeDefaults) {
  Harness h;
  h.start(popupConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  QQuickItem* surface = findItem(popup, QStringLiteral("popupSurface"));
  ASSERT_NE(surface, nullptr);
  EXPECT_EQ(surface->property("radius").toInt(), 8);  // Theme.popupRadius
  EXPECT_DOUBLE_EQ(popup->property("gap").toDouble(), 6);
  const QObject* border = surface->property("border").value<QObject*>();
  ASSERT_NE(border, nullptr);
  EXPECT_EQ(border->property("width").toInt(), 1);
}

TEST(PopupWindowTest, ZeroPaddingLetsTheContentFillTheWindow) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [
                Item {
                    id: anchorBox
                    width: 60
                    height: 24
                    PopupWindow {
                        objectName: "flat"
                        anchor.item: anchorBox
                        padding: 0
                        Rectangle { objectName: "block"; width: 120; height: 50; color: "red" }
                    }
                }
            ]
        }
    }
}
)"));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("flat"));
  invoke(popup, "open");
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return popup->width() == 120 && popup->height() == 50; }, 2000));
  EXPECT_EQ(popup->width(), 120);
  EXPECT_EQ(popup->height(), 50);
}

TEST(PopupWindowTest, ItDoesNotOpenWithoutAnAnchor) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import waku
ShellRoot { Panel { content: PanelLayout { start: [ Item { PopupWindow { objectName: "orphan" } } ] } } }
)"));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("orphan"));
  ASSERT_NE(popup, nullptr);
  invoke(popup, "open");
  EXPECT_FALSE(popup->isVisible());
}

// ---- Tooltip ----

TEST(TooltipTest, AppearsAfterTheDelayOfHoveringAndGoesWhenThePointerLeaves) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  EXPECT_EQ(findWindow(QStringLiteral("tooltipWindow")), nullptr);  // created on the first show

  hoverOver(h, QStringLiteral("target"));
  EXPECT_EQ(findWindow(QStringLiteral("tooltipWindow")), nullptr);  // not yet: the delay
  EXPECT_TRUE(tooltipPending(h, QStringLiteral("tip")));            // the hover registered at once
  QQuickWindow* tip = waitForTooltipWindow();
  ASSERT_NE(tip, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));

  QTest::mouseMove(h.window(), QPoint(h.window()->width() - 10, 30));
  EXPECT_TRUE(QTest::qWaitFor([&] { return !tip->isVisible(); }, 2000));
}

TEST(TooltipTest, ShowsBelowTheBarAndSizesToItsText) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  hoverOver(h, QStringLiteral("target"));
  QQuickWindow* tip = waitForTooltipWindow();
  ASSERT_NE(tip, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));
  // The platform reports the new geometry asynchronously.
  EXPECT_TRUE(QTest::qWaitFor(
      [&] { return tip->y() >= h.window()->y() + h.window()->height() && tip->width() > 0; },
      2000));
  EXPECT_GE(tip->y(), h.window()->y() + h.window()->height());
  EXPECT_GT(tip->width(), 0);
  EXPECT_GT(tip->height(), 0);
}

TEST(TooltipTest, APressHidesIt) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  const QPoint at = centerOf(h.window(), QStringLiteral("target"));
  hoverOver(h, QStringLiteral("target"));
  QQuickWindow* tip = waitForTooltipWindow();
  ASSERT_NE(tip, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));
  QTest::mousePress(h.window(), Qt::LeftButton, Qt::NoModifier, at);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !tip->isVisible(); }, 2000));
  QTest::mouseRelease(h.window(), Qt::LeftButton, Qt::NoModifier, at);
}

TEST(TooltipTest, EmptyTextNeverShows) {
  Harness h;
  h.start(tooltipConfig(QString()));
  ASSERT_TRUE(started(h));
  hoverOver(h, QStringLiteral("target"));
  // With text the hover starts the delay at once (AppearsAfterTheDelay...); without, nothing is
  // waiting to show the tip.
  EXPECT_FALSE(tooltipPending(h, QStringLiteral("tip")));
  EXPECT_EQ(findWindow(QStringLiteral("tooltipWindow")), nullptr);
}

TEST(TooltipTest, ItGetsTheBackdropOfItsBar) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  const size_t before = h.native.backdrops.size();
  hoverOver(h, QStringLiteral("target"));
  QQuickWindow* tip = waitForTooltipWindow();
  ASSERT_NE(tip, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));
  EXPECT_GT(h.native.backdrops.size(), before);
}

TEST(PopupWindowTest, ImplicitWidthAndHeightSizeTheWindowAsInQuickshell) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import waku
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [
                Item {
                    id: anchorBox
                    width: 60
                    height: 24
                    PopupWindow {
                        objectName: "pop"
                        anchor.item: anchorBox
                        implicitWidth: 150
                        implicitHeight: 90
                        Text { text: "hello"; color: "white" }
                    }
                }
            ]
        }
    }
}
)"));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  ASSERT_NE(popup, nullptr);
  invoke(popup, "open");
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return popup->width() == 150 && popup->height() == 90; }, 2000));
}

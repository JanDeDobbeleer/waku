#include <gtest/gtest.h>

#include "spike/bar/popup_placement.h"
#include "spike/tests/bar_fixtures.h"

namespace {

QString popupConfig(const QString& barProperties = QString(),
                    const QString& group = QStringLiteral("start")) {
  return QStringLiteral(R"(
import QtQuick
import barnacle
Config {
    Bar {
        %1
        layout: BarLayout {
            %2: [
                Item {
                    id: anchor
                    objectName: "anchor"
                    width: 60
                    height: 24
                    PopupWindow {
                        objectName: "pop"
                        anchorItem: anchor
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
import barnacle
Config {
    Bar {
        layout: BarLayout {
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
  QTest::qWait(20);
  QTest::mouseMove(h.window(), centerOf(h.window(), name));
}

bool started(Harness& h) {
  if (!h.host->reload()) return false;
  return h.window() && QTest::qWaitForWindowExposed(h.window());
}

void invoke(QObject* object, const char* method) {
  ASSERT_TRUE(QMetaObject::invokeMethod(object, method));
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
  h.start(popupConfig(QStringLiteral("height: 40")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  QSignalSpy opened(popup, SIGNAL(opened()));
  invoke(popup, "open");
  EXPECT_TRUE(popup->isVisible());
  EXPECT_EQ(opened.count(), 1);
  QTest::qWait(50);

  EXPECT_GE(popup->y(), 40);  // below the bar
  QQuickItem* content = findItem(popup, QStringLiteral("content"));
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(popup->width(), qCeil(content->implicitWidth()) + 20);  // the default padding
  EXPECT_EQ(popup->height(), qCeil(content->implicitHeight()) + 20);
  const QRect screen = QGuiApplication::primaryScreen()->geometry();
  EXPECT_GE(popup->x(), screen.x() + 8);
  EXPECT_LE(popup->x() + popup->width(), screen.right() - 8 + 1);
}

TEST(PopupWindowTest, ABottomBarOpensItAboveTheWidget) {
  Harness h;
  h.start(popupConfig(QStringLiteral("height: 40; position: Bar.Bottom")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  QTest::qWait(50);
  EXPECT_LE(popup->y() + popup->height(), h.window()->y());
}

TEST(PopupWindowTest, AWidgetAtTheScreenEdgeKeepsItOnScreen) {
  Harness h;
  h.start(popupConfig(QString(), QStringLiteral("end")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  QTest::qWait(50);
  const QRect screen = QGuiApplication::primaryScreen()->geometry();
  EXPECT_LE(popup->x() + popup->width(), screen.right() - 8 + 1);
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
  QTest::qWait(300);
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
  QTest::qWait(50);
  EXPECT_FALSE(popup->isVisible());
}

TEST(PopupWindowTest, ItGetsTheBackdropOfItsBar) {
  Harness h;
  h.start(popupConfig(QStringLiteral("backdrop: Bar.Blur")));
  ASSERT_TRUE(started(h));
  const size_t before = h.native.backdrops.size();
  QQuickWindow* popup = findWindow(QStringLiteral("pop"));
  invoke(popup, "open");
  ASSERT_EQ(h.native.backdrops.size(), before + 1);
  EXPECT_EQ(h.native.backdrops.back(), Bar::Blur);
}

TEST(PopupWindowTest, ItDoesNotOpenWithoutAnAnchor) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import barnacle
Config { Bar { layout: BarLayout { start: [ Item { PopupWindow { objectName: "orphan" } } ] } } }
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
  QQuickWindow* tip = findWindow(QStringLiteral("tooltipWindow"));
  ASSERT_NE(tip, nullptr);
  EXPECT_FALSE(tip->isVisible());

  hoverOver(h, QStringLiteral("target"));
  EXPECT_FALSE(tip->isVisible());  // not yet: the delay
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));

  QTest::mouseMove(h.window(), QPoint(h.window()->width() - 10, 30));
  EXPECT_TRUE(QTest::qWaitFor([&] { return !tip->isVisible(); }, 2000));
}

TEST(TooltipTest, ShowsBelowTheBarAndSizesToItsText) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* tip = findWindow(QStringLiteral("tooltipWindow"));
  hoverOver(h, QStringLiteral("target"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));
  QTest::qWait(30);
  EXPECT_GE(tip->y(), h.window()->y() + h.window()->height());
  EXPECT_GT(tip->width(), 0);
  EXPECT_GT(tip->height(), 0);
}

TEST(TooltipTest, APressHidesIt) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  QQuickWindow* tip = findWindow(QStringLiteral("tooltipWindow"));
  const QPoint at = centerOf(h.window(), QStringLiteral("target"));
  hoverOver(h, QStringLiteral("target"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));
  QTest::mousePress(h.window(), Qt::LeftButton, Qt::NoModifier, at);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !tip->isVisible(); }, 2000));
  QTest::mouseRelease(h.window(), Qt::LeftButton, Qt::NoModifier, at);
}

TEST(TooltipTest, EmptyTextNeverShows) {
  Harness h;
  h.start(tooltipConfig(QString()));
  ASSERT_TRUE(started(h));
  QQuickWindow* tip = findWindow(QStringLiteral("tooltipWindow"));
  hoverOver(h, QStringLiteral("target"));
  QTest::qWait(300);
  EXPECT_FALSE(tip->isVisible());
}

TEST(TooltipTest, ItGetsTheBackdropOfItsBar) {
  Harness h;
  h.start(tooltipConfig());
  ASSERT_TRUE(started(h));
  const size_t before = h.native.backdrops.size();
  QQuickWindow* tip = findWindow(QStringLiteral("tooltipWindow"));
  hoverOver(h, QStringLiteral("target"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 2000));
  EXPECT_GT(h.native.backdrops.size(), before);
}

// WindowThumbnail and the rectangle maths behind it, on the fake backend: no test registers a real
// DWM thumbnail.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>

#include "host/qml/window_thumbnail.h"
#include "host/testing/thumbnail_fakes.h"
#include "host/thumbnail.h"

using namespace waku::host;

namespace {

// Installs a fake backend for the length of a test.
struct ScopedBackend {
  ScopedBackend() { WindowThumbnail::setBackendForTesting(&fake); }
  ~ScopedBackend() { WindowThumbnail::setBackendForTesting(nullptr); }
  FakeThumbnailBackend fake;
};

struct Fixture {
  Fixture() {
    window.resize(800, 600);
    item = new WindowThumbnail(window.contentItem());
    item->setPosition(QPointF(40, 20));
    item->setSize(QSizeF(320, 180));
  }
  void show() {
    window.show();
    QCoreApplication::processEvents();
  }
  void settle() {
    QCoreApplication::processEvents();
    item->syncNow();
  }
  QQuickWindow window;
  WindowThumbnail* item = nullptr;  // owned by the window's content item
};

}  // namespace

TEST(WindowThumbnailMath, ScalesTheRectangleByTheDevicePixelRatio) {
  const QRect at15 = thumbnailDestination(QRectF(40, 20, 320, 180), 1.5, QSize(), false);
  EXPECT_EQ(at15, QRect(60, 30, 480, 270));
  const QRect at20 = thumbnailDestination(QRectF(40, 20, 320, 180), 2.0, QSize(), false);
  EXPECT_EQ(at20, QRect(80, 40, 640, 360));
}

TEST(WindowThumbnailMath, RoundsEdgesNotSizes) {
  // 10.3 * 1.5 = 15.45 -> 15; 110.3 * 1.5 = 165.45 -> 165: the width is 150, not 100 * 1.5 rounded
  // separately from the origin.
  const QRect rect = thumbnailDestination(QRectF(10.3, 0, 100, 10), 1.5, QSize(), false);
  EXPECT_EQ(rect.left(), 15);
  EXPECT_EQ(rect.width(), 150);
}

TEST(WindowThumbnailMath, FitsAWideWindowIntoASquareAndCentresIt) {
  // 1600x800 (2:1) into 200x200 at the origin, ratio 1: 200x100, centred vertically.
  const QRect rect = thumbnailDestination(QRectF(0, 0, 200, 200), 1.0, QSize(1600, 800), true);
  EXPECT_EQ(rect, QRect(0, 50, 200, 100));
}

TEST(WindowThumbnailMath, FitsATallWindowAtRatio2WithAnOffset) {
  // The item is 100x100 at (10, 20) = 200x200 physical at (20, 40); a 500x1000 window becomes
  // 100x200, centred horizontally.
  const QRect rect = thumbnailDestination(QRectF(10, 20, 100, 100), 2.0, QSize(500, 1000), true);
  EXPECT_EQ(rect, QRect(20 + 50, 40, 100, 200));
}

TEST(WindowThumbnailMath, StretchFillsAndEmptyInputGivesAnEmptyRectangle) {
  EXPECT_EQ(thumbnailDestination(QRectF(0, 0, 200, 200), 1.0, QSize(1600, 800), false),
            QRect(0, 0, 200, 200));
  EXPECT_TRUE(thumbnailDestination(QRectF(5, 5, 0, 40), 1.0, QSize(10, 10), true).isEmpty());
  // A broken ratio counts as 1.
  EXPECT_EQ(thumbnailDestination(QRectF(0, 0, 10, 10), 0.0, QSize(), false), QRect(0, 0, 10, 10));
}

TEST(WindowThumbnail, IsInertWhenTheDesktopIsNotLive) {
  // Offscreen and no backend installed: no registration, nothing available.
  Fixture f;
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  EXPECT_FALSE(f.item->available());
  EXPECT_EQ(f.item->sourceWidth(), 0);
}

TEST(WindowThumbnail, RegistersAndPlacesTheThumbnailInTheWindow) {
  ScopedBackend backend;
  Fixture f;
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  const auto* entry = backend.fake.only();
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->source, 1234u);
  EXPECT_EQ(entry->destination, static_cast<quintptr>(f.window.winId()));
  EXPECT_TRUE(f.item->available());
  EXPECT_EQ(f.item->sourceWidth(), 1920);
  EXPECT_EQ(f.item->sourceHeight(), 1080);
  // 320x180 holds 1920x1080 exactly; the item sits at (40, 20) in the window.
  EXPECT_EQ(entry->props.destination, QRect(40, 20, 320, 180));
  EXPECT_TRUE(entry->props.visible);
  EXPECT_EQ(entry->props.opacity, 255);
}

TEST(WindowThumbnail, FollowsGeometryFitAndOpacity) {
  ScopedBackend backend;
  Fixture f;
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  f.item->setWidth(200);
  f.item->setHeight(200);
  f.settle();
  // 16:9 into 200x200 at (40, 20): 200x113 (112.5 rounds to 113), centred.
  EXPECT_EQ(backend.fake.only()->props.destination, QRect(40, 20 + 43, 200, 113));
  f.item->setFit(WindowThumbnail::Stretch);
  f.settle();
  EXPECT_EQ(backend.fake.only()->props.destination, QRect(40, 20, 200, 200));
  f.item->setOpacity(0.5);
  f.settle();
  EXPECT_EQ(backend.fake.only()->props.opacity, 128);
  f.item->setVisible(false);
  f.settle();
  EXPECT_FALSE(backend.fake.only()->props.visible);
}

TEST(WindowThumbnail, FollowsAnAncestorThatMoves) {
  ScopedBackend backend;
  Fixture f;
  auto* holder = new QQuickItem(f.window.contentItem());
  f.item->setParentItem(holder);
  holder->setPosition(QPointF(100, 100));
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  EXPECT_EQ(backend.fake.only()->props.destination.topLeft(), QPoint(140, 120));
  holder->setOpacity(0.5);
  holder->setX(200);
  f.settle();
  EXPECT_EQ(backend.fake.only()->props.destination.topLeft(), QPoint(240, 120));
  EXPECT_EQ(backend.fake.only()->props.opacity, 128);
}

TEST(WindowThumbnail, ChangingTheHwndRegistersAgain) {
  ScopedBackend backend;
  Fixture f;
  f.item->setHwnd(1);
  f.show();
  f.settle();
  f.item->setHwnd(2);
  f.settle();
  ASSERT_NE(backend.fake.only(), nullptr);
  EXPECT_EQ(backend.fake.only()->source, 2u);
  EXPECT_EQ(backend.fake.calls.count(QStringLiteral("register")), 2);
  EXPECT_EQ(backend.fake.calls.count(QStringLiteral("unregister")), 1);
  f.item->setHwnd(0);
  f.settle();
  EXPECT_TRUE(backend.fake.entries.empty());
  EXPECT_FALSE(f.item->available());
}

TEST(WindowThumbnail, UnregistersWhenTheWindowHidesAndRegistersWhenItShows) {
  ScopedBackend backend;
  Fixture f;
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  ASSERT_NE(backend.fake.only(), nullptr);
  f.window.hide();
  QCoreApplication::processEvents();
  EXPECT_TRUE(backend.fake.entries.empty());
  EXPECT_FALSE(f.item->available());
  f.show();
  f.settle();
  EXPECT_NE(backend.fake.only(), nullptr);
}

TEST(WindowThumbnail, UnregistersWhenTheItemLeavesTheWindowOrIsDestroyed) {
  ScopedBackend backend;
  {
    Fixture f;
    f.item->setHwnd(1234);
    f.show();
    f.settle();
    ASSERT_NE(backend.fake.only(), nullptr);
    f.item->setParentItem(nullptr);
    EXPECT_TRUE(backend.fake.entries.empty());
    f.item->setParentItem(f.window.contentItem());
    f.settle();
    EXPECT_NE(backend.fake.only(), nullptr);
  }
  // The fixture's destruction took the item and its registration with it.
  EXPECT_TRUE(backend.fake.entries.empty());
}

TEST(WindowThumbnail, MakesNoCallWhileInactive) {
  ScopedBackend backend;
  Fixture f;
  f.item->setActive(false);
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  f.item->setWidth(100);
  f.settle();
  EXPECT_TRUE(backend.fake.calls.isEmpty());
  f.item->setActive(true);
  f.settle();
  EXPECT_NE(backend.fake.only(), nullptr);
  f.item->setActive(false);
  EXPECT_TRUE(backend.fake.entries.empty());
  EXPECT_FALSE(f.item->available());
}

TEST(WindowThumbnail, AFailedRegistrationLeavesTheItemUnavailableAndRetries) {
  ScopedBackend backend;
  backend.fake.failRegistration = true;
  Fixture f;
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  EXPECT_FALSE(f.item->available());
  EXPECT_EQ(f.item->sourceWidth(), 0);
  backend.fake.failRegistration = false;
  f.item->syncNow();  // what the one-second timer does
  EXPECT_TRUE(f.item->available());
}

TEST(WindowThumbnail, AClosedSourceWindowMakesItUnavailable) {
  ScopedBackend backend;
  Fixture f;
  f.item->setHwnd(1234);
  f.show();
  f.settle();
  ASSERT_TRUE(f.item->available());
  backend.fake.entries.clear();  // the source window closed: DWM dropped the thumbnail
  f.item->syncNow();
  EXPECT_FALSE(f.item->available());
  EXPECT_EQ(f.item->sourceHeight(), 0);
}

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <memory>
#include <vector>

#include "features/notifications/qml/notification_feed.h"
#include "features/notifications/qml/notification_service.h"
#include "features/notifications/testing/fake_notification_source.h"

using namespace wabar::notifications;

namespace {

NotificationItem toast(std::uint32_t id, const QString& title, const QString& scenario = {}) {
  NotificationItem item;
  item.id = id;
  item.aumid = QStringLiteral("Contoso.Mail");
  item.appName = QStringLiteral("Mail");
  item.title = title;
  item.body = QStringLiteral("Body");
  item.logo = QStringLiteral("C:/icons/mail.png");
  item.image = QStringLiteral("C:/images/photo.png");
  item.scenario = scenario;
  item.createdAt = QDateTime::currentDateTimeUtc();
  return item;
}

struct Harness {
  Harness() {
    auto fake = std::make_unique<FakeSource>();
    source = fake.get();
    service = std::make_shared<NotificationService>(std::move(fake));
    hub = std::make_shared<NotificationHub>(service);
    feed = std::make_unique<NotificationFeed>(hub, nullptr);
  }
  void publish(std::vector<NotificationItem> items) {
    source->sink_->itemsChanged(std::move(items));
    QCoreApplication::processEvents();
  }
  FakeSource* source = nullptr;
  std::shared_ptr<NotificationService> service;
  std::shared_ptr<NotificationHub> hub;
  std::unique_ptr<NotificationFeed> feed;
};

}  // namespace

TEST(NotificationFeed, IsEmptyAtFirst) {
  Harness h;
  EXPECT_TRUE(h.feed->available());
  EXPECT_EQ(h.feed->notifications()->count(), 0);
}

TEST(NotificationFeed, WithoutASourceIsUnavailable) {
  auto service = std::make_shared<NotificationService>(nullptr);
  NotificationFeed feed(std::make_shared<NotificationHub>(service), nullptr);
  EXPECT_FALSE(feed.available());
  feed.clear();
  feed.refresh();
  EXPECT_EQ(feed.notifications()->count(), 0);
}

TEST(NotificationFeed, ToastAddedFiresForANewToastWithItsFields) {
  Harness h;
  QSignalSpy added(h.feed.get(), &NotificationFeed::toastAdded);
  h.publish({toast(7, QStringLiteral("Hello"))});
  ASSERT_EQ(added.count(), 1);
  auto* t = qvariant_cast<NotificationToast*>(added.at(0).at(0));
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->id(), 7);
  EXPECT_EQ(t->appName(), QStringLiteral("Mail"));
  EXPECT_EQ(t->summary(), QStringLiteral("Hello"));
  EXPECT_EQ(t->body(), QStringLiteral("Body"));
  EXPECT_EQ(t->appIcon(), QStringLiteral("C:/icons/mail.png"));
  EXPECT_EQ(t->image(), QStringLiteral("C:/images/photo.png"));
  EXPECT_EQ(t->urgency(), NotificationUrgency::Normal);
  EXPECT_TRUE(t->time().isValid());
  EXPECT_FALSE(t->timeText().isEmpty());
  EXPECT_EQ(h.feed->notifications()->count(), 1);
  EXPECT_EQ(h.feed->notifications()->values().at(0), t);
}

TEST(NotificationFeed, ToastsAlreadyInTheActionCenterFillTheModelSilently) {
  Harness h;
  QSignalSpy added(h.feed.get(), &NotificationFeed::toastAdded);
  NotificationItem old = toast(1, QStringLiteral("Old"));
  old.createdAt = QDateTime::currentDateTimeUtc().addSecs(-3600);
  h.publish({old});
  EXPECT_EQ(added.count(), 0);
  EXPECT_EQ(h.feed->notifications()->count(), 1);
}

TEST(NotificationFeed, ToastAddedFiresOncePerToast) {
  Harness h;
  QSignalSpy added(h.feed.get(), &NotificationFeed::toastAdded);
  h.publish({toast(1, QStringLiteral("One"))});
  h.publish({toast(2, QStringLiteral("Two")), toast(1, QStringLiteral("One"))});
  EXPECT_EQ(added.count(), 2);
  ASSERT_EQ(h.feed->notifications()->count(), 2);
  EXPECT_EQ(h.feed->notifications()->values().at(0)->id(), 2);  // newest first
  EXPECT_EQ(h.feed->notifications()->values().at(1)->id(), 1);
}

TEST(NotificationFeed, UrgencyFollowsTheToastScenario) {
  EXPECT_EQ(urgencyForScenario({}), NotificationUrgency::Normal);
  EXPECT_EQ(urgencyForScenario(QStringLiteral("reminder")), NotificationUrgency::Normal);
  EXPECT_EQ(urgencyForScenario(QStringLiteral("urgent")), NotificationUrgency::Critical);
  EXPECT_EQ(urgencyForScenario(QStringLiteral("alarm")), NotificationUrgency::Critical);
  EXPECT_EQ(urgencyForScenario(QStringLiteral("incomingCall")), NotificationUrgency::Critical);
  Harness h;
  h.publish({toast(1, QStringLiteral("Call"), QStringLiteral("incomingCall"))});
  EXPECT_EQ(h.feed->notifications()->values().at(0)->urgency(), NotificationUrgency::Critical);
}

TEST(NotificationFeed, ChangedFieldsEmitTheirSignals) {
  Harness h;
  h.publish({toast(1, QStringLiteral("One"))});
  NotificationToast* t = h.feed->notifications()->values().at(0);
  QSignalSpy summary(t, &NotificationToast::summaryChanged);
  QSignalSpy body(t, &NotificationToast::bodyChanged);
  NotificationItem changed = toast(1, QStringLiteral("Two"));
  changed.createdAt = t->item().createdAt;
  h.publish({changed});
  EXPECT_EQ(summary.count(), 1);
  EXPECT_EQ(body.count(), 0);
  EXPECT_EQ(t->summary(), QStringLiteral("Two"));
  EXPECT_EQ(h.feed->notifications()->values().at(0), t);
}

TEST(NotificationFeed, ARemovedToastClosesAndLeavesTheModel) {
  Harness h;
  h.publish({toast(1, QStringLiteral("One")), toast(2, QStringLiteral("Two"))});
  NotificationToast* one = h.feed->notifications()->find(1);
  ASSERT_NE(one, nullptr);
  QSignalSpy closed(one, &NotificationToast::closed);
  h.publish({toast(2, QStringLiteral("Two"))});
  EXPECT_EQ(closed.count(), 1);
  EXPECT_EQ(h.feed->notifications()->count(), 1);
  EXPECT_EQ(h.feed->notifications()->find(1), nullptr);
}

TEST(NotificationFeed, DismissCloseActivateAndClearReachTheSource) {
  Harness h;
  h.publish({toast(5, QStringLiteral("One")), toast(6, QStringLiteral("Two"))});
  NotificationToast* t = h.feed->notifications()->find(5);
  t->dismiss();
  h.feed->notifications()->find(6)->close();
  t->activate();
  h.feed->clear();
  h.feed->refresh();
  ASSERT_EQ(h.source->removed.size(), 2u);
  EXPECT_EQ(h.source->removed[0], 5u);
  EXPECT_EQ(h.source->removed[1], 6u);
  ASSERT_EQ(h.source->activated.size(), 1u);
  EXPECT_EQ(h.source->activated[0], 5u);
  EXPECT_EQ(h.source->clears, 1);
  EXPECT_GE(h.source->refreshes, 1);
}

TEST(NotificationFeed, OperatingSystemOwnedMembersStayEmpty) {
  Harness h;
  h.publish({toast(1, QStringLiteral("One"))});
  NotificationToast* t = h.feed->notifications()->values().at(0);
  EXPECT_TRUE(t->actions().isEmpty());
  EXPECT_TRUE(t->hints().isEmpty());
  EXPECT_FALSE(t->resident());
  EXPECT_FALSE(t->isTransient());
  EXPECT_FALSE(t->hasActionIcons());
  EXPECT_FALSE(t->hasInlineReply());
  EXPECT_EQ(t->expireTimeout(), 0);
  t->expire();
  EXPECT_TRUE(h.source->removed.empty());
}

TEST(NotificationServer, EmitsNotificationForNewToastsAndTracksThem) {
  Harness h;
  NotificationServer server(h.hub, nullptr);
  NotificationToast* seen = nullptr;
  QObject::connect(&server, &NotificationServer::notification, &server, [&](NotificationToast* t) {
    seen = t;
    t->setTracked(true);
  });
  h.publish({toast(3, QStringLiteral("Three"))});
  ASSERT_NE(seen, nullptr);
  EXPECT_EQ(seen->id(), 3);
  EXPECT_EQ(server.trackedNotifications()->count(), 1);
  seen->setTracked(false);
  EXPECT_EQ(server.trackedNotifications()->count(), 0);
}

TEST(NotificationServer, IsUsableFromQmlWithoutASource) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import Quickshell.Services.Notifications
NotificationServer {
    keepOnReload: false
    actionsSupported: true
    bodyMarkupSupported: true
    imageSupported: true
    property int low: NotificationUrgency.Low
    property int critical: NotificationUrgency.Critical
    property int dismissed: NotificationCloseReason.Dismissed
    onNotification: notif => { notif.tracked = true; }
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_EQ(root->property("low").toInt(), 0);
  EXPECT_EQ(root->property("critical").toInt(), 2);
  EXPECT_EQ(root->property("dismissed").toInt(), 2);
  EXPECT_FALSE(root->property("keepOnReload").toBool());
  EXPECT_TRUE(root->property("actionsSupported").toBool());
}

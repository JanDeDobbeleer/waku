#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <memory>
#include <vector>

#include "spike/services/notification_service.h"

using namespace wabar::services;
namespace platform = yasb::platform;

namespace {

class FakeSource final : public platform::NotificationSource {
 public:
  void start(platform::NotificationSink* sink) override { sink_ = sink; }
  void stop() override { sink_ = nullptr; }
  void clearAll() override { ++clears; }
  void refresh() override { ++refreshes; }
  void remove(std::uint32_t id) override { removed.push_back(id); }
  void activate(const platform::NotificationItem& item) override { activated.push_back(item.id); }

  platform::NotificationSink* sink_ = nullptr;
  int clears = 0;
  int refreshes = 0;
  std::vector<std::uint32_t> removed;
  std::vector<std::uint32_t> activated;
};

platform::NotificationItem toast(std::uint32_t id, const QString& title) {
  platform::NotificationItem item;
  item.id = id;
  item.appName = QStringLiteral("Mail");
  item.title = title;
  item.body = QStringLiteral("Body");
  item.createdAt = QDateTime::currentDateTime();
  return item;
}

struct Harness {
  Harness() {
    auto fake = std::make_unique<FakeSource>();
    source = fake.get();
    notifications = std::make_unique<Notifications>(std::move(fake), nullptr);
  }
  void publish(std::uint32_t count, std::vector<platform::NotificationItem> items) {
    source->sink_->countChanged(count);
    source->sink_->itemsChanged(std::move(items));
    QCoreApplication::processEvents();
  }
  FakeSource* source = nullptr;
  std::unique_ptr<Notifications> notifications;
};

}  // namespace

TEST(NotificationsService, IsEmptyAtFirst) {
  Harness h;
  EXPECT_TRUE(h.notifications->available());
  EXPECT_EQ(h.notifications->count(), 0);
  EXPECT_FALSE(h.notifications->unread());
  EXPECT_TRUE(h.notifications->items().isEmpty());
}

TEST(NotificationsService, WithoutASourceIsUnavailable) {
  Notifications none(nullptr, nullptr);
  EXPECT_FALSE(none.available());
  none.clear();
  none.refresh();
  none.dismiss(1);
  none.activate(1);
  EXPECT_EQ(none.count(), 0);
}

TEST(NotificationsService, ExposesCountUnreadAndItems) {
  Harness h;
  QSignalSpy changed(h.notifications.get(), &Notifications::changed);
  h.publish(2, {toast(7, QStringLiteral("Hello")), toast(3, QStringLiteral("World"))});
  EXPECT_GE(changed.count(), 1);
  EXPECT_EQ(h.notifications->count(), 2);
  EXPECT_TRUE(h.notifications->unread());
  const QVariantList items = h.notifications->items();
  ASSERT_EQ(items.size(), 2);
  const QVariantMap first = items.at(0).toMap();
  EXPECT_EQ(first.value(QStringLiteral("id")).toInt(), 7);
  EXPECT_EQ(first.value(QStringLiteral("app")).toString(), QStringLiteral("Mail"));
  EXPECT_EQ(first.value(QStringLiteral("title")).toString(), QStringLiteral("Hello"));
  EXPECT_EQ(first.value(QStringLiteral("body")).toString(), QStringLiteral("Body"));
  EXPECT_FALSE(first.value(QStringLiteral("time")).toString().isEmpty());

  h.publish(0, {});
  EXPECT_FALSE(h.notifications->unread());
  EXPECT_TRUE(h.notifications->items().isEmpty());
}

TEST(NotificationsService, ForwardsCommandsToTheSource) {
  Harness h;
  h.publish(1, {toast(7, QStringLiteral("Hello"))});
  h.notifications->clear();
  h.notifications->refresh();
  h.notifications->dismiss(7);
  h.notifications->activate(7);
  h.notifications->activate(99);  // unknown id: ignored
  EXPECT_EQ(h.source->clears, 1);
  EXPECT_EQ(h.source->refreshes, 1);
  EXPECT_EQ(h.source->removed, std::vector<std::uint32_t>{7});
  EXPECT_EQ(h.source->activated, std::vector<std::uint32_t>{7});
}

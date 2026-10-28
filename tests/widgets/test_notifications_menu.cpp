#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <nlohmann/json.hpp>

#include "core/bar/theme_class.h"
#include "core/config/loader.h"
#include "core/widgets/backdrop.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "platform/dnd.h"
#include "platform/notifications.h"
#include "platform/toast_activation.h"
#include "platform/toast_xml.h"
#include "widgets/notifications/notifications_menu.h"
#include "widgets/notifications/notifications_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

// --- toast XML -------------------------------------------------------------------------------

const char* kToastXml =
    "<toast launch=\"action=open&amp;id=7\" activationType=\"protocol\" "
    "displayTimestamp=\"2026-03-05T09:30:00+02:00\">"
    "<visual><binding template=\"ToastGeneric\">"
    "<text>Title</text><text> Body line </text><text>  </text>"
    "<text placement=\"attribution\">via Mail</text>"
    "<image placement=\"appLogoOverride\" hint-crop=\"circle\" src=\"file:///C:/logo.png\"/>"
    "<image placement=\"hero\" src=\"C:\\hero.png\"/>"
    "<image src=\"https://example.com/a.png\"/>"
    "</binding></visual></toast>";

TEST(ToastXml, ParsesTextsAttributionLaunchAndImages) {
  const auto toast = parseToastXml(QString::fromLatin1(kToastXml));
  ASSERT_TRUE(toast.has_value());
  EXPECT_EQ(toast->texts, (QStringList{"Title", "Body line"}));
  EXPECT_EQ(toast->attribution, "via Mail");
  EXPECT_EQ(toast->launch, "action=open&id=7");
  EXPECT_EQ(toast->activationType, "protocol");
  ASSERT_TRUE(toast->displayTimestamp.has_value());
  EXPECT_EQ(toast->displayTimestamp->toUTC(),
            QDateTime(QDate(2026, 3, 5), QTime(7, 30), QTimeZone::UTC));
  ASSERT_NE(toast->findImage("appLogoOverride"), nullptr);
  EXPECT_EQ(toast->findImage("appLogoOverride")->hintCrop, "circle");
  ASSERT_NE(toast->findImage("hero"), nullptr);
  ASSERT_NE(toast->findInlineImage(), nullptr);
  EXPECT_EQ(toast->findInlineImage()->src, "https://example.com/a.png");
  EXPECT_EQ(toast->findImage("missing"), nullptr);
}

TEST(ToastXml, DefaultsAndBrokenXml) {
  const auto bare =
      parseToastXml("<toast><visual><binding template=\"ToastGeneric\"/></visual></toast>");
  ASSERT_TRUE(bare.has_value());
  EXPECT_TRUE(bare->texts.isEmpty());
  EXPECT_EQ(bare->activationType, "foreground");
  EXPECT_FALSE(bare->displayTimestamp.has_value());
  EXPECT_FALSE(parseToastXml("<toast><visual>").has_value());
  EXPECT_FALSE(parseToastXml("").has_value());
}

TEST(ToastXml, LegacyImageTemplatesShowTheirImageAsTheLogo) {
  const auto toast = parseToastXml(
      "<toast><visual><binding template=\"ToastImageAndText01\"><image id=\"1\" src=\"C:\\a.png\"/>"
      "<text id=\"1\">Hi</text></binding></visual></toast>");
  ASSERT_TRUE(toast.has_value());
  ASSERT_NE(toast->findImage("appLogoOverride"), nullptr);
  EXPECT_EQ(toast->findImage("appLogoOverride")->src, "C:\\a.png");
  EXPECT_EQ(toast->findInlineImage(), nullptr);
  EXPECT_EQ(toast->findImage("hero"), nullptr);
}

TEST(ToastXml, PopToastMatchesByTextAndRemovesIt) {
  std::vector<ToastXml> toasts(3);
  toasts[0].texts = {"a"};
  toasts[1].texts = {"b", "c"};
  toasts[1].launch = "second";
  toasts[2].texts = {"b", "c"};
  toasts[2].launch = "third";
  EXPECT_EQ(popToast(toasts, {"b", "c"})->launch, "second");
  EXPECT_EQ(popToast(toasts, {"b", "c"})->launch, "third");
  EXPECT_FALSE(popToast(toasts, {"b", "c"}).has_value());
  EXPECT_EQ(toasts.size(), 1u);
}

class ImagePaths : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    root_ = dir_.path();
    QDir().mkpath(root_ + "/Microsoft/Windows/Notifications/wpnidm");
    QDir().mkpath(root_ + "/Packages/Fam_1/LocalState/img");
    QDir().mkpath(root_ + "/install/assets");
    for (const char* path :
         {"/Microsoft/Windows/Notifications/wpnidm/dl.png",
          "/Packages/Fam_1/LocalState/img/data.png", "/install/assets/logo.png", "/outside.png"}) {
      QImage image(4, 4, QImage::Format_ARGB32);
      image.fill(Qt::red);
      ASSERT_TRUE(image.save(root_ + path));
    }
    env_.family = "Fam_1";
    env_.localAppData = root_;
    env_.installFolder = [this] { return root_ + "/install"; };
    env_.downloadedImage = [this](const QString& url) {
      if (url == "https://example.com/dl.png")
        return root_ + "/Microsoft/Windows/Notifications/wpnidm/dl.png";
      if (url == "https://example.com/escape.png") return root_ + "/outside.png";
      return QString();
    };
  }

  QString resolve(const QString& src) {
    const ToastImage image{"", src, ""};
    return resolveToastImagePath(&image, env_);
  }

  QTemporaryDir dir_;
  QString root_;
  ImagePathEnv env_;
};

TEST_F(ImagePaths, ResolvesLocalFileSources) {
  const QString native =
      QDir::toNativeSeparators(QFileInfo(root_ + "/outside.png").canonicalFilePath());
  EXPECT_EQ(resolve("file:///" + root_ + "/outside.png"), native);
  EXPECT_EQ(resolve(root_ + "/outside.png"), native);
  EXPECT_EQ(resolve("file:///" + root_ + "/missing.png"), "");
}

TEST_F(ImagePaths, RejectsSharesAndRelativePaths) {
  EXPECT_EQ(resolve("file://server/share/a.png"), "");
  EXPECT_EQ(resolve("\\\\server\\share\\a.png"), "");
  EXPECT_EQ(resolve("relative/a.png"), "");
  EXPECT_EQ(resolve("ftp://example.com/a.png"), "");
  EXPECT_EQ(resolve(""), "");
}

TEST_F(ImagePaths, PackagedSchemesStayInsideTheirRoot) {
  EXPECT_NE(resolve("ms-appx:///assets/logo.png"), "");
  EXPECT_EQ(resolve("ms-appx:///../outside.png"), "");
  EXPECT_NE(resolve("ms-appdata:///local/img/data.png"), "");
  EXPECT_EQ(resolve("ms-appdata:///roaming/img/data.png"), "");
  env_.family.clear();
  EXPECT_EQ(resolve("ms-appx:///assets/logo.png"), "");
}

TEST_F(ImagePaths, WebImagesNeedWindowsCopyInsideTheDownloadFolder) {
  EXPECT_NE(resolve("https://example.com/dl.png"), "");
  EXPECT_EQ(resolve("https://example.com/escape.png"), "");
  EXPECT_EQ(resolve("https://example.com/unknown.png"), "");
}

TEST(AssembleItems, JoinsListenerTextsToHistoryXmlNewestFirst) {
  std::vector<RawToast> raw(3);
  raw[0] = {3,
            "app.one",
            "App One",
            "",
            QDateTime(QDate(2026, 1, 1), QTime(10, 0), QTimeZone::UTC),
            {"Same", "newest"}};
  raw[1] = {2,
            "app.one",
            "App One",
            "",
            QDateTime(QDate(2026, 1, 1), QTime(9, 0), QTimeZone::UTC),
            {"Same", "older"}};
  raw[2] = {1, "", "", "", QDateTime(QDate(2026, 1, 1), QTime(8, 0), QTimeZone::UTC), {"Orphan"}};
  int historyCalls = 0;
  int askedCount = 0;
  const auto history = [&](const QString& aumid, int count) {
    ++historyCalls;
    askedCount = count;
    EXPECT_EQ(aumid, "app.one");
    std::vector<ToastXml> toasts(2);
    toasts[0].texts = {"Same", "older"};
    toasts[0].launch = "older-launch";
    toasts[0].attribution = "attr";
    toasts[1].texts = {"Same", "newest"};
    toasts[1].launch = "newest-launch";
    toasts[1].displayTimestamp = QDateTime(QDate(2026, 1, 1), QTime(10, 0, 5), QTimeZone::UTC);
    return toasts;
  };
  const auto items =
      assembleItems(raw, history, [](const QString&, const QString&) { return ImagePathEnv{}; });
  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(historyCalls, 1);  // once per app, never for an empty AUMID
  EXPECT_EQ(askedCount, 2);
  EXPECT_EQ(items[0].launch, "newest-launch");
  EXPECT_EQ(items[0].title, "Same");
  EXPECT_EQ(items[0].body, "newest");
  EXPECT_EQ(items[0].createdAt, QDateTime(QDate(2026, 1, 1), QTime(10, 0, 5), QTimeZone::UTC));
  EXPECT_EQ(items[1].launch, "older-launch");
  EXPECT_EQ(items[1].attribution, "attr");
  EXPECT_EQ(items[1].createdAt, raw[1].created);  // no displayTimestamp: the listener's time
  EXPECT_EQ(items[2].launch, "");
  EXPECT_EQ(items[2].activationType, "foreground");
  EXPECT_EQ(items[2].title, "Orphan");
}

TEST(ToastActivation, OnlyRealLinksOpenAsProtocolActivation) {
  EXPECT_TRUE(isOpenableToastUri("https://example.com/x"));
  EXPECT_TRUE(isOpenableToastUri("ms-settings:network"));
  EXPECT_TRUE(isOpenableToastUri("myapp://open?id=1"));
  EXPECT_FALSE(isOpenableToastUri("file:///C:/Windows/notepad.exe"));
  EXPECT_FALSE(isOpenableToastUri("FILE:///C:/x"));
  EXPECT_FALSE(isOpenableToastUri("C:\\Windows\\notepad.exe"));  // a drive letter is no scheme
  EXPECT_FALSE(isOpenableToastUri("action=open"));
  EXPECT_FALSE(isOpenableToastUri(""));
  EXPECT_FALSE(activateToast("app", "x", "system"));
}

TEST(ShortRelativeTime, MatchesTimeUtilsShortFormat) {
  const QDateTime now(QDate(2026, 10, 28), QTime(12, 0), QTimeZone::UTC);
  const auto ago = [&](qint64 seconds) { return shortRelativeTime(now.addSecs(-seconds), now); };
  EXPECT_EQ(ago(0), "now");
  EXPECT_EQ(ago(59), "now");
  EXPECT_EQ(ago(-30), "now");  // a toast stamped slightly in the future
  EXPECT_EQ(ago(60), "1m");
  EXPECT_EQ(ago(59 * 60), "59m");
  EXPECT_EQ(ago(3600), "1h");
  EXPECT_EQ(ago(23 * 3600 + 3599), "23h");
  EXPECT_EQ(ago(24 * 3600), "1d");
  EXPECT_EQ(ago(6 * 24 * 3600 + 100), "6d");
  EXPECT_EQ(ago(7 * 24 * 3600), "21 Oct");
  EXPECT_EQ(shortRelativeTime(QDateTime(QDate(2026, 3, 5), QTime(1, 0), QTimeZone::UTC), now),
            "5 Mar");
  EXPECT_EQ(shortRelativeTime(QDateTime(QDate(2025, 10, 28), QTime(11, 0), QTimeZone::UTC), now),
            "Oct 2025");
  EXPECT_EQ(shortRelativeTime(QDateTime(), now), "");
}

// --- service ---------------------------------------------------------------------------------

class FakeSource final : public NotificationSource {
 public:
  void start(NotificationSink* sink) override { sink_ = sink; }
  void stop() override { sink_ = nullptr; }
  void clearAll() override { ++clearAlls; }
  void refresh() override { ++refreshes; }
  void remove(std::uint32_t id) override { removed.push_back(id); }
  void clearApp(const QString& aumid) override { clearedApps << aumid; }
  void activate(const NotificationItem& item) override { activated.push_back(item); }

  NotificationSink* sink_ = nullptr;
  int clearAlls = 0;
  int refreshes = 0;
  std::vector<std::uint32_t> removed;
  QStringList clearedApps;
  std::vector<NotificationItem> activated;
};

NotificationItem makeItem(std::uint32_t id, const QString& aumid, const QString& app,
                          const QString& title, const QString& body = {}) {
  NotificationItem item;
  item.id = id;
  item.aumid = aumid;
  item.appName = app;
  item.title = title;
  item.body = body;
  item.createdAt = QDateTime::currentDateTimeUtc().addSecs(-5 * 60);
  return item;
}

class MenuTest : public testing::Test {
 protected:
  void SetUp() override {
    auto source = std::make_unique<FakeSource>();
    source_ = source.get();
    service_ = std::make_unique<NotificationService>(std::move(source));
    service_->start();
    dnd_ = "disabled";
    opened_.clear();
    centerToggles_ = 0;
  }

  void TearDown() override {
    widget_.reset();
    service_.reset();
  }

  NotificationsWidget& build(const json& extra = json::object()) {
    json options = {
        {"label", "<span>{icon}</span>"},
        {"label_alt", "{count} notifications"},
        {"tooltip", false},
        {"icons",
         {{"new", "N"}, {"default", "D"}, {"dnd_on", "ON"}, {"dnd_off", "OFF"}, {"dismiss", "X"}}},
        {"callbacks", {{"on_left", "toggle_menu"}}}};
    for (const auto& [key, value] : extra.items()) options[key] = value;
    const json config = {
        {"bars", {{"main", {{"widgets", {{"left", {"notifications"}}}}}}}},
        {"widgets",
         {{"notifications",
           {{"type", "yasb.notifications.NotificationsWidget"}, {"options", options}}}}}};
    const auto loaded = yasb::config::loadConfigText(config.dump());
    EXPECT_TRUE(loaded.ok()) << loaded.message;
    TypedWidgetOptions typed =
        loaded.widgetOptions<yasb::config::options::NotificationsConfig>("notifications").value();
    WidgetContext context{"notifications",
                          "yasb.notifications.NotificationsWidget",
                          options,
                          {},
                          {},
                          eventBus_,
                          globalState_,
                          pollingWorkers_,
                          std::move(typed)};
    NotificationsMenuHooks hooks;
    hooks.dndStatus = [this] { return dnd_; };
    hooks.setDndStatus = [this](const QString& mode) { dnd_ = mode; };
    hooks.openUri = [this](const QString& uri) { opened_ << uri; };
    hooks.toggleNotificationCenter = [this] { ++centerToggles_; };
    widget_.reset(new NotificationsWidget(context, service_.get(), nullptr, std::move(hooks)));
    widget_->show();
    return *widget_;
  }

  void sendItems(std::optional<std::vector<NotificationItem>> items) {
    source_->sink_->itemsChanged(std::move(items));
    QCoreApplication::processEvents();
  }

  static void click(QWidget* widget, Qt::MouseButton button = Qt::LeftButton) {
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(2, 2),
                      QPointF(widget->mapToGlobal(QPoint(2, 2))), button, button, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
  }

  static QList<QLabel*> labels(QWidget* root, const QString& cssClass) {
    QList<QLabel*> found;
    for (QLabel* label : root->findChildren<QLabel*>()) {
      if (label->property("class").toString() == cssClass) found << label;
    }
    return found;
  }

  static QList<QWidget*> frames(QWidget* root, const QString& cssClass) {
    QList<QWidget*> found;
    for (QWidget* widget : root->findChildren<QWidget*>()) {
      if (widget->property("class").toString() == cssClass) found << widget;
    }
    return found;
  }

  static bool waitFor(const std::function<bool()>& done, int timeoutMs = 3000) {
    for (int waited = 0; waited < timeoutMs; waited += 20) {
      if (done()) return true;
      QTest::qWait(20);
    }
    return done();
  }

  FakeSource* source_ = nullptr;
  std::unique_ptr<NotificationService> service_;
  std::unique_ptr<NotificationsWidget> widget_;
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
  QString dnd_;
  QStringList opened_;
  int centerToggles_ = 0;
};

TEST_F(MenuTest, ServiceEmitsItemsOnlyWhenTheListChanges) {
  QSignalSpy spy(service_.get(), &NotificationService::itemsChanged);
  sendItems(std::vector<NotificationItem>{});  // equal to the initial empty list
  EXPECT_EQ(spy.count(), 0);
  sendItems(std::vector<NotificationItem>{makeItem(1, "a", "A", "t")});
  sendItems(std::vector<NotificationItem>{makeItem(1, "a", "A", "t")});
  EXPECT_EQ(spy.count(), 1);
  sendItems(std::nullopt);
  EXPECT_EQ(spy.count(), 2);
  EXPECT_FALSE(service_->items().has_value());
  service_->stop();
  service_->remove(4);
  EXPECT_TRUE(source_->removed.empty());
}

TEST_F(MenuTest, ToggleMenuOpensAndClosesThePopupAndAsksForARefresh) {
  NotificationsWidget& widget = build();
  EXPECT_TRUE(widget.runCallback("toggle_menu"));
  MenuPopup* popup = widget.menu()->popup();
  ASSERT_NE(popup, nullptr);
  EXPECT_TRUE(popup->isVisible());
  EXPECT_EQ(popup->property("class").toString(), "notification-menu");
  EXPECT_EQ(source_->refreshes, 1);
  EXPECT_TRUE(widget.runCallback("toggle_menu"));
  EXPECT_TRUE(popup->isClosing());
}

TEST_F(MenuTest, EmptyMenuShowsHeaderFooterAndMessage) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  ASSERT_NE(popup, nullptr);
  ASSERT_EQ(frames(popup, "header").size(), 1);
  ASSERT_EQ(frames(popup, "footer").size(), 1);
  EXPECT_EQ(labels(popup, "title").first()->text(), "Notifications");
  EXPECT_EQ(labels(popup, "clear-all").first()->text(), "Clear all");
  EXPECT_EQ(labels(popup, "label").first()->text(), "Open Notification Center");
  ASSERT_EQ(frames(popup, "empty").size(), 1);
  EXPECT_EQ(labels(popup, "text").first()->text(), "No new notifications");
  EXPECT_EQ(labels(popup, "icon").first()->text(), "D");
  EXPECT_TRUE(labels(popup, "link").isEmpty());
  EXPECT_EQ(frames(popup, "contents").size(), 1);
}

TEST_F(MenuTest, PermissionDeniedShowsTheAccessMessageAndSettingsLink) {
  NotificationsWidget& widget = build();
  sendItems(std::nullopt);  // the listener reports access turned off
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  ASSERT_NE(popup, nullptr);
  ASSERT_EQ(labels(popup, "text").size(), 1);
  EXPECT_EQ(labels(popup, "text").first()->text(), "Notification access is turned off");
  ASSERT_EQ(labels(popup, "link").size(), 1);
  EXPECT_EQ(labels(popup, "link").first()->text(), "Open settings");

  click(labels(popup, "link").first());
  EXPECT_EQ(opened_, QStringList{"ms-settings:privacy-notifications"});
  EXPECT_TRUE(popup->property("class").toString() == "notification-menu");
  EXPECT_TRUE(widget.menu()->popup()->isClosing());

  // Access turned back on: the next list replaces the message.
  source_->sink_->itemsChanged(std::vector<NotificationItem>{});
  QCoreApplication::processEvents();
  EXPECT_EQ(service_->items()->size(), 0u);
}

TEST_F(MenuTest, GroupsToastsPerAppWithPythonClassNames) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  sendItems(std::vector<NotificationItem>{
      makeItem(5, "mail", "Mail", "Hello", "World"),
      makeItem(4, "chat", "Chat", "Ping"),
      makeItem(3, "mail", "Mail", "Again"),
      makeItem(2, "", "", "Anon"),
  });
  ASSERT_EQ(frames(popup, "section").size(), 3);  // mail, chat, "" in first-seen order
  ASSERT_EQ(frames(popup, "section-header").size(), 3);
  const auto names = labels(popup, "name");
  ASSERT_EQ(names.size(), 3);
  EXPECT_EQ(names[0]->text(), "Mail");
  EXPECT_EQ(names[1]->text(), "Chat");
  EXPECT_EQ(names[2]->text(), "Other");  // a sender Windows cannot name
  EXPECT_EQ(frames(popup, "item").size(), 4);
  ASSERT_EQ(labels(popup, "body").size(), 1);
  EXPECT_EQ(labels(popup, "body").first()->text(), "World");
  EXPECT_EQ(labels(popup, "time").first()->text(), "5m");
  EXPECT_EQ(labels(popup, "dismiss").size(), 4);
  EXPECT_EQ(labels(popup, "clear-app").size(), 3);
  EXPECT_EQ(labels(popup, "dismiss").first()->text(), "X");
  EXPECT_TRUE(labels(popup, "more").isEmpty());
  EXPECT_TRUE(frames(popup, "empty").isEmpty());
}

TEST_F(MenuTest, MoreButtonExpandsAndCollapsesAnApp) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  std::vector<NotificationItem> items;
  for (std::uint32_t id = 6; id >= 1; --id) items.push_back(makeItem(id, "mail", "Mail", "m"));
  sendItems(items);
  EXPECT_EQ(frames(popup, "item").size(), 2);
  ASSERT_EQ(labels(popup, "more").size(), 1);
  EXPECT_EQ(labels(popup, "more").first()->text(), "4 more");

  click(labels(popup, "more").first());
  EXPECT_EQ(frames(popup, "item").size(), 6);
  ASSERT_EQ(labels(popup, "more").size(), 1);
  EXPECT_EQ(labels(popup, "more").first()->text(), "Show less");

  click(labels(popup, "more").first());
  EXPECT_EQ(frames(popup, "item").size(), 2);
  EXPECT_EQ(labels(popup, "more").first()->text(), "4 more");
}

TEST_F(MenuTest, DismissButtonsReachTheSourceWithoutActivatingTheToast) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  sendItems(std::vector<NotificationItem>{makeItem(9, "mail", "Mail", "Hi"),
                                          makeItem(8, "chat", "Chat", "Yo")});
  click(labels(popup, "dismiss").first());
  EXPECT_EQ(source_->removed, std::vector<std::uint32_t>{9});
  EXPECT_TRUE(source_->activated.empty());
  EXPECT_FALSE(widget.menu()->popup()->isClosing());

  click(labels(popup, "clear-app").last());
  EXPECT_EQ(source_->clearedApps, QStringList{"chat"});

  click(labels(popup, "clear-all").first());
  EXPECT_EQ(source_->clearAlls, 1);
}

TEST_F(MenuTest, ClickingAToastClosesTheMenuAndActivatesIt) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  NotificationItem item = makeItem(11, "mail", "Mail", "Open me");
  item.launch = "launch-args";
  item.activationType = "protocol";
  sendItems(std::vector<NotificationItem>{item});
  click(frames(popup, "item").first());
  ASSERT_EQ(source_->activated.size(), 1u);
  EXPECT_EQ(source_->activated[0], item);
  EXPECT_TRUE(widget.menu()->popup()->isClosing());
}

TEST_F(MenuTest, FooterOpensTheNotificationCenter) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  click(labels(popup, "label").first());
  EXPECT_EQ(centerToggles_, 1);
  EXPECT_TRUE(widget.menu()->popup()->isClosing());
}

TEST_F(MenuTest, EveryListChangeRefreshesTheOpenMenuWithoutACountChange) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  sendItems(std::vector<NotificationItem>{makeItem(1, "a", "A", "one")});
  EXPECT_EQ(frames(popup, "item").size(), 1);
  EXPECT_EQ(widget.count(), 0);  // the WNF count never moved
  sendItems(
      std::vector<NotificationItem>{makeItem(2, "a", "A", "two"), makeItem(1, "a", "A", "one")});
  EXPECT_EQ(frames(popup, "item").size(), 2);
  sendItems(std::vector<NotificationItem>{});
  EXPECT_EQ(frames(popup, "item").size(), 0);
  EXPECT_EQ(frames(popup, "empty").size(), 1);
}

TEST_F(MenuTest, DndButtonFollowsFocusAssistAndToggles) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QLabel* button = widget.menu()->dndButton();
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(button->text(), "OFF");
  EXPECT_EQ(button->property("class").toString(), "dnd");

  click(button);
  EXPECT_EQ(dnd_, "priority");
  EXPECT_EQ(button->text(), "ON");
  EXPECT_EQ(button->property("class").toString(), "dnd active");

  click(button);
  EXPECT_EQ(dnd_, "disabled");
  EXPECT_EQ(button->text(), "OFF");
  EXPECT_EQ(button->property("class").toString(), "dnd");

  dnd_ = "alarms";
  click(button);  // any non-disabled mode toggles to disabled
  EXPECT_EQ(dnd_, "disabled");
}

TEST_F(MenuTest, DndButtonIsMissingWhenFocusAssistIsUnreadable) {
  dnd_ = "unknown";
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  EXPECT_EQ(widget.menu()->dndButton(), nullptr);
  EXPECT_TRUE(labels(widget.menu()->popup(), "dnd").isEmpty());
}

TEST_F(MenuTest, DndWatcherEventsUpdateTheButton) {
  auto watcher = std::make_unique<DndWatcher>();
  NotificationsMenuOptions options;
  options.iconDndOn = "ON";
  options.iconDndOff = "OFF";
  options.tooltip = false;
  NotificationsMenuHooks hooks;
  hooks.dndStatus = [] { return QStringLiteral("disabled"); };
  hooks.setDndStatus = [](const QString&) {};
  QWidget anchor;
  anchor.show();
  NotificationsMenu menu(&anchor, service_.get(), watcher.get(), options, hooks, globalState_);
  menu.toggle();
  ASSERT_NE(menu.dndButton(), nullptr);
  watcher->deliver("priority");
  EXPECT_EQ(menu.dndButton()->text(), "ON");
  EXPECT_EQ(menu.dndButton()->property("class").toString(), "dnd active");
  watcher->deliver("disabled");
  EXPECT_EQ(menu.dndButton()->text(), "OFF");
  menu.close();
}

TEST_F(MenuTest, ToastPicturesLoadOffThreadAndAppearInTheOpenMenu) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString logo = dir.path() + "/logo.png";
  const QString hero = dir.path() + "/hero.png";
  QImage square(96, 64, QImage::Format_ARGB32);
  square.fill(Qt::blue);
  ASSERT_TRUE(square.save(logo));
  ASSERT_TRUE(square.save(hero));

  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  NotificationItem item = makeItem(1, "", "", "With pictures");
  item.logo = logo;
  item.logoCircle = true;
  item.hero = hero;
  sendItems(std::vector<NotificationItem>{item});
  EXPECT_TRUE(labels(popup, "avatar").isEmpty());  // still loading
  ASSERT_TRUE(waitFor([&] { return !labels(popup, "avatar").isEmpty(); }));
  EXPECT_EQ(frames(popup, "hero").size(), 1);
  EXPECT_EQ(labels(popup, "avatar").first()->pixmap().deviceIndependentSize().width(), 48);
}

TEST_F(MenuTest, UnreadableToastPictureLeavesNoPlaceholder) {
  NotificationsWidget& widget = build();
  widget.menu()->toggle();
  QWidget* popup = widget.menu()->popup();
  NotificationItem item = makeItem(1, "", "", "Broken");
  item.logo = "C:/definitely/not/here.png";
  sendItems(std::vector<NotificationItem>{item});
  QTest::qWait(100);
  EXPECT_TRUE(labels(popup, "avatar").isEmpty());
  EXPECT_EQ(frames(popup, "item").size(), 1);
}

}  // namespace

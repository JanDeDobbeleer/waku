#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <deque>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/config/loader.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "core/widgets/widget_builder.h"
#include "platform/github_api.h"
#include "widgets/github/github_data_store.h"
#include "widgets/github/github_format.h"
#include "widgets/github/github_widget.h"
#include "widgets/stat_widgets.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

// --- fake HTTP ---------------------------------------------------------------------------------

class FakeTransport final : public HttpTransport {
 public:
  struct Route {
    QString urlPart;
    QByteArray verb;
    HttpResponse response;
  };
  void add(const QString& urlPart, HttpResponse response, QByteArray verb = "GET") {
    routes_.push_back({urlPart, std::move(verb), std::move(response)});
  }
  HttpResponse send(const HttpRequest& request) override {
    requests.push_back(request);
    for (const Route& route : routes_) {
      if (request.verb == route.verb && request.url.contains(route.urlPart)) return route.response;
    }
    HttpResponse missing;
    missing.error = QStringLiteral("no route");
    return missing;
  }
  std::vector<HttpRequest> requests;

 private:
  std::vector<Route> routes_;
};

HttpResponse ok(const QByteArray& body, const QByteArray& link = {}) {
  HttpResponse response;
  response.status = 200;
  response.reason = QStringLiteral("OK");
  response.body = body;
  response.link = link;
  return response;
}

QByteArray notificationJson(const char* id, const char* type, const char* subjectPath,
                            const char* repo, bool unread, const char* reason = "mention",
                            const char* updated = "2026-10-27T12:00:00Z",
                            const char* title = "A title") {
  return QByteArray(R"({"id":")") + id + R"(","unread":)" + (unread ? "true" : "false") +
         R"(,"reason":")" + reason + R"(","updated_at":")" + updated + R"(","subject":{"title":")" +
         title + R"(","type":")" + type + R"(","url":")" + subjectPath +
         R"("},"repository":{"full_name":")" + repo + R"(","html_url":"https://github.com/)" +
         repo + R"("}})";
}

QByteArray pageOf(const std::vector<QByteArray>& items) {
  QByteArray out = "[";
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out += ",";
    out += items[i];
  }
  return out + "]";
}

GithubNotification note(const QString& id, const QString& type, bool unread,
                        const QString& title = QStringLiteral("Title")) {
  GithubNotification n;
  n.id = id;
  n.type = type;
  n.unread = unread;
  n.title = title;
  n.repository = QStringLiteral("octo/hello");
  n.url = QStringLiteral("https://github.com/octo/hello/") + id;
  n.updatedAt = QStringLiteral("2026-10-25T12:00:00Z");
  return n;
}

// --- widget harness ----------------------------------------------------------------------------

yasb::config::options::GithubConfig loadOptions(json options) {
  json doc = {{"bars", {{"main", {{"widgets", {{"left", {"gh"}}}}}}}},
              {"widgets", {{"gh", {{"type", "yasb.github.GithubWidget"}, {"options", options}}}}}};
  const auto config = yasb::config::loadConfigText(doc.dump());
  EXPECT_TRUE(config.ok()) << config.message;
  auto typed = config.widgetOptions<yasb::config::options::GithubConfig>("gh");
  EXPECT_TRUE(typed.has_value());
  return typed.value_or(yasb::config::options::GithubConfig{});
}

const QDateTime kNow = QDateTime(QDate(2026, 10, 28), QTime(12, 0), QTimeZone::UTC);

struct Calls {
  std::vector<std::pair<QString, QString>> threads;
  std::vector<QString> allTokens;
  std::vector<QUrl> urls;
  int logins = 0;
};

}  // namespace

class GithubWidgetTest : public testing::Test {
 protected:
  std::unique_ptr<GithubWidget> build(json options, std::optional<QString> token = QString("t")) {
    GithubWidgetServices services;
    services.token = token ? token : std::optional<QString>(QString());
    services.startPolling = false;
    services.store = &store_;
    services.markThreadRead = [this](const QString& t, const QString& id) {
      calls_.threads.emplace_back(t, id);
    };
    services.markAllRead = [this](const QString& t) { calls_.allTokens.push_back(t); };
    services.openUrl = [this](const QUrl& url) { calls_.urls.push_back(url); };
    services.nowUtc = [] { return kNow; };
    services.startLogin = [this] { ++calls_.logins; };
    TypedWidgetOptions typed = loadOptions(std::move(options));
    return std::make_unique<GithubWidget>(WidgetContext{"gh",
                                                        "yasb.github.GithubWidget",
                                                        {},
                                                        {},
                                                        {},
                                                        eventBus_,
                                                        globalState_,
                                                        pollingWorkers_,
                                                        std::move(typed)},
                                          std::move(services));
  }

  static json menuOptions() {
    return {{"label", "<span></span>"},
            {"label_alt", "Notifications {data}"},
            {"show_comment_count", true},
            {"max_field_size", 54},
            {"menu", {{"blur", false}, {"show_categories", true}}}};
  }

  static QStringList classesOf(const QWidget* w) {
    return w->property("class").toString().split(' ', Qt::SkipEmptyParts);
  }

  QList<QLabel*> labelsWithClass(QWidget* root, const QString& cssClass) {
    QList<QLabel*> out;
    for (QLabel* label : root->findChildren<QLabel*>()) {
      if (classesOf(label).contains(cssClass)) out << label;
    }
    return out;
  }

  QList<QFrame*> items(QWidget* root) {
    QList<QFrame*> out;
    for (QFrame* frame : root->findChildren<QFrame*>()) {
      if (classesOf(frame).contains("item")) out << frame;
    }
    return out;
  }

  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
  GithubDataStore store_;
  Calls calls_;
};

// --- label vectors -----------------------------------------------------------------------------

namespace {

json labelState(QLabel* label) {
  return {{"text", label->text().toStdString()},
          {"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()}};
}

json snapshot(GithubWidget& widget) {
  json primary = json::array();
  json alt = json::array();
  for (QLabel* l : widget.primaryLabels()) primary.push_back(labelState(l));
  for (QLabel* l : widget.altLabels()) alt.push_back(labelState(l));
  return {{"widget_hidden", widget.isHidden()}, {"primary", primary}, {"alt", alt}};
}

void expectSnapshot(const json& actual, const json& expected, const std::string& where) {
  EXPECT_EQ(actual["widget_hidden"], expected["widget_hidden"]) << where;
  EXPECT_EQ(actual["primary"], expected["primary"]) << where;
  EXPECT_EQ(actual["alt"], expected["alt"]) << where;
}

}  // namespace

TEST_F(GithubWidgetTest, ReplaysPythonLabelVectors) {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/github.json", std::ios::binary);
  ASSERT_TRUE(in.good());
  const json scenarios = json::parse(in);
  ASSERT_GE(scenarios.size(), 7u);
  for (const json& scenario : scenarios) {
    const std::string name = scenario["scenario"];
    ASSERT_FALSE(scenario.contains("construct_error")) << name;
    GithubDataStore store;
    GithubWidgetServices services;
    services.token = QString("placeholder");
    services.startPolling = false;
    services.store = &store;
    TypedWidgetOptions typed = loadOptions(scenario["options"]);
    GithubWidget widget(WidgetContext{"gh",
                                      "yasb.github.GithubWidget",
                                      {},
                                      {},
                                      {},
                                      eventBus_,
                                      globalState_,
                                      pollingWorkers_,
                                      std::move(typed)},
                        std::move(services));
    widget.show();
    expectSnapshot(snapshot(widget), scenario["initial"], name + " initial");
    int index = 0;
    for (const json& record : scenario["steps"]) {
      const json& step = record["step"];
      if (step.value("toggle", false)) {
        widget.runCallback("toggle_label");
      } else {
        std::vector<GithubNotification> list;
        for (int i = 0; i < step.value("unread", 0); ++i) list.push_back(note("u", "Issue", true));
        for (int i = 0; i < step.value("read", 0); ++i) list.push_back(note("r", "Issue", false));
        store.setNotifications(std::move(list));
      }
      expectSnapshot(snapshot(widget), record["snapshot"],
                     name + " step " + std::to_string(index++));
    }
  }
}

// --- widget behaviour --------------------------------------------------------------------------

TEST_F(GithubWidgetTest, UserLabelHasNotificationLabelAndDotFollowsUnreadCount) {
  auto widget = build(
      {{"label", "<span></span>"},
       {"notification_dot",
        {{"enabled", true}, {"corner", "top_right"}, {"color", "#ef4747"}, {"margin", {0, 5}}}}});
  widget->show();
  NotificationLabel* dot = widget->notificationLabel(false);
  ASSERT_NE(dot, nullptr);
  EXPECT_FALSE(dot->dotVisible());
  store_.setNotifications({note("1", "Issue", true)});
  EXPECT_TRUE(dot->dotVisible());
  store_.setNotifications({note("1", "Issue", false)});
  EXPECT_FALSE(dot->dotVisible());
}

TEST_F(GithubWidgetTest, DotOriginFollowsCornerAndMargin) {
  NotificationLabel topRight("x", "red", "top_right", {0, 5});
  topRight.resize(30, 20);
  EXPECT_EQ(topRight.dotOrigin(), QPoint(24, 5));
  NotificationLabel bottomLeft("x", "red", "bottom_left", {1, 2});
  bottomLeft.resize(30, 20);
  EXPECT_EQ(bottomLeft.dotOrigin(), QPoint(1, 12));
  NotificationLabel bottomRight("x", "red", "bottom_right", {3, 4});
  bottomRight.resize(30, 20);
  EXPECT_EQ(bottomRight.dotOrigin(), QPoint(21, 10));
  NotificationLabel topLeft("x", "red", "top_left", {7, 8});
  EXPECT_EQ(topLeft.dotOrigin(), QPoint(7, 8));
}

TEST_F(GithubWidgetTest, DefaultsToToggleMenuAndToggleLabelCallbacks) {
  auto widget = build(menuOptions());
  EXPECT_TRUE(widget->callbacks().contains(QStringLiteral("toggle_menu")));
  EXPECT_TRUE(widget->callbacks().contains(QStringLiteral("toggle_label")));
}

TEST_F(GithubWidgetTest, MenuWithoutTokenStartsLoginInsteadOfShowingMenu) {
  auto widget = build(menuOptions(), QString());
  EXPECT_FALSE(widget->hasToken());
  widget->runCallback("toggle_menu");
  EXPECT_EQ(calls_.logins, 1);
  EXPECT_EQ(widget->menu(), nullptr);
}

TEST_F(GithubWidgetTest, MenuGroupsByConfiguredCategoryOrderThenTheRest) {
  json options = menuOptions();
  options["menu"]["categories_order"] = {"Issue", "PullRequest"};
  auto widget = build(options);
  widget->show();
  store_.setNotifications({note("1", "Release", true), note("2", "PullRequest", true),
                           note("3", "Issue", true), note("4", "issue", false),
                           note("5", "WeirdThing", false)});
  widget->runCallback("toggle_menu");
  ASSERT_NE(widget->menu(), nullptr);

  QStringList headers;
  for (QLabel* l : labelsWithClass(widget->menu(), "section-header")) headers << l->text();
  // Python keeps the last key per lower-cased name ("issue"), so the configured "Issue" slot\n  //
  // holds it and the capitalised "Issue" group falls into the remainder.
  EXPECT_EQ(headers, (QStringList{"issue", "Pull Requests", "Releases", "Issues", "Weird Thing"}));
}

TEST_F(GithubWidgetTest, MenuItemsCarryClassesIconsTextAndCommentCount) {
  json options = menuOptions();
  options["menu"]["categories_order"] = {"Issue", "PullRequest"};
  options["icons"] = {{"comment", ""}};
  auto widget = build(options);
  widget->show();

  GithubNotification closed = note("1", "Issue", true, "Fix the thing");
  closed.issueState = QString("closed");
  closed.commentCount = 7;
  GithubNotification merged = note("2", "PullRequest", false, "Merge me");
  merged.pullRequestState = QString("closed");
  merged.pullRequestIsMerged = true;
  GithubNotification second = note("3", "Issue", false, "Second issue");
  store_.setNotifications({closed, merged, second});
  widget->showMenu();
  PinnablePopup* menu = widget->menu();
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(menu->property("class").toString(), "github-menu");

  const QList<QFrame*> all = items(menu);
  ASSERT_EQ(all.size(), 3);
  EXPECT_EQ(all[0]->property("class").toString(), "item new issue first");
  EXPECT_EQ(all[1]->property("class").toString(), "item issue last");
  EXPECT_EQ(all[2]->property("class").toString(), "item pullrequest first last");

  const QList<QLabel*> icons = labelsWithClass(menu, "icon");
  ASSERT_EQ(icons.size(), 3);
  EXPECT_EQ(icons[0]->property("class").toString(), "icon issue closed");
  EXPECT_EQ(icons[2]->property("class").toString(), "icon pullrequest merged closed");

  const QList<QLabel*> titles = labelsWithClass(menu, "title");
  EXPECT_EQ(titles[0]->text(), "Fix the thing");
  const QList<QLabel*> descriptions = labelsWithClass(menu, "description");
  EXPECT_EQ(descriptions[0]->text(), QString::fromUtf8("octo/hello • Updated 3 days ago"));
  EXPECT_EQ(labelsWithClass(menu, "comment-count").size(), 1);
  EXPECT_EQ(labelsWithClass(menu, "comment-count")[0]->text(), "7");
  EXPECT_EQ(labelsWithClass(menu, "comment-icon").size(), 1);

  QStringList footer;
  for (QLabel* l : labelsWithClass(menu, "label")) footer << l->text();
  EXPECT_EQ(footer, (QStringList{"Unread notifications (1)", "Mark all as read"}));
}

TEST_F(GithubWidgetTest, FlatMenuPrefixesTypeAndTruncatesToMaxFieldSize) {
  json options = menuOptions();
  options["menu"]["show_categories"] = false;
  options["max_field_size"] = 20;
  auto widget = build(options);
  store_.setNotifications({note("1", "Issue", true, "A very long notification title here")});
  widget->showMenu();
  EXPECT_TRUE(labelsWithClass(widget->menu(), "section-header").isEmpty());
  EXPECT_EQ(labelsWithClass(widget->menu(), "title")[0]->text(), "A very long notif...");
  EXPECT_EQ(labelsWithClass(widget->menu(), "description")[0]->text().size(), 20);
  EXPECT_TRUE(labelsWithClass(widget->menu(), "description")[0]->text().endsWith("..."));
}

TEST_F(GithubWidgetTest, ClickingItemMarksReadOpensUrlAndHidesMenu) {
  auto widget = build(menuOptions());
  widget->show();
  store_.setNotifications({note("42", "Issue", true), note("43", "Issue", true)});
  widget->showMenu();
  PinnablePopup* menu = widget->menu();
  QFrame* first = items(menu)[0];
  QTest::mouseClick(first, Qt::LeftButton);

  ASSERT_EQ(calls_.threads.size(), 1u);
  EXPECT_EQ(calls_.threads[0].first, "t");
  EXPECT_EQ(calls_.threads[0].second, "42");
  ASSERT_EQ(calls_.urls.size(), 1u);
  EXPECT_EQ(calls_.urls[0], QUrl("https://github.com/octo/hello/42"));
  EXPECT_FALSE(classesOf(first).contains("new"));
  EXPECT_FALSE(menu->isVisible());
  EXPECT_EQ(store_.unreadCount(), 1);
  EXPECT_TRUE(widget->notificationLabel(false)->dotVisible());
}

TEST_F(GithubWidgetTest, MarkAllAsReadClearsUnreadAndHidesMenu) {
  auto widget = build(menuOptions());
  widget->show();
  store_.setNotifications({note("1", "Issue", true), note("2", "Release", true)});
  widget->showMenu();
  QLabel* markAll = nullptr;
  for (QLabel* l : labelsWithClass(widget->menu(), "label")) {
    if (l->text() == "Mark all as read") markAll = l;
  }
  ASSERT_NE(markAll, nullptr);
  QTest::mouseClick(markAll, Qt::LeftButton);
  ASSERT_EQ(calls_.allTokens.size(), 1u);
  EXPECT_EQ(store_.unreadCount(), 0);
  EXPECT_FALSE(widget->menu()->isVisible());
  EXPECT_FALSE(widget->notificationLabel(false)->dotVisible());
}

TEST_F(GithubWidgetTest, EmptyMenuShowsLogoAndNoFooter) {
  auto widget = build(menuOptions());
  widget->showMenu();
  QStringList texts;
  for (QLabel* l : widget->menu()->findChildren<QLabel*>()) texts << l->text();
  EXPECT_TRUE(texts.contains("No unread notifications"));
  EXPECT_TRUE(labelsWithClass(widget->menu(), "label").isEmpty());
  EXPECT_TRUE(items(widget->menu()).isEmpty());
}

TEST_F(GithubWidgetTest, SetTokenAfterLoginEnablesTheMenu) {
  auto widget = build(menuOptions(), QString());
  widget->setToken("fresh");
  EXPECT_TRUE(widget->hasToken());
  widget->runCallback("toggle_menu");
  EXPECT_EQ(calls_.logins, 0);
  EXPECT_NE(widget->menu(), nullptr);
}

TEST_F(GithubWidgetTest, BuilderCreatesTheWidgetFromConfig) {
  const auto config = yasb::config::loadConfigText(R"(
bars:
  main:
    widgets:
      left: [gh]
widgets:
  gh:
    type: yasb.github.GithubWidget
    options:
      label: "<span></span>"
      update_interval: 120
)");
  ASSERT_TRUE(config.ok()) << config.message;
  WidgetRegistry registry;
  registerStatWidgets(registry);
  EXPECT_TRUE(registry.contains("yasb.github.GithubWidget"));
  // Never touch the real token file: point LOCALAPPDATA at an empty folder so no poll runs.
  QTemporaryDir emptyAppData;
  ASSERT_TRUE(emptyAppData.isValid());
  const QByteArray previous = qgetenv("LOCALAPPDATA");
  qputenv("LOCALAPPDATA", emptyAppData.path().toUtf8());
  WidgetBuilder builder(config, registry, eventBus_, globalState_, pollingWorkers_);
  std::unique_ptr<QWidget> built(builder.buildWidget("gh", {}, {}));
  qputenv("LOCALAPPDATA", previous);
  ASSERT_NE(built, nullptr);
  auto* widget = qobject_cast<GithubWidget*>(built.get());
  ASSERT_NE(widget, nullptr);
  EXPECT_FALSE(widget->hasToken());
  pollingWorkers_.stopAll();
}

TEST(GithubStore, FailedFetchClearsListAndSkippedPollKeepsIt) {
  GithubDataStore store;
  store.setNotifications({note("1", "Issue", true)});
  GithubFetchResult skipped;
  store.applyFetch(skipped);
  EXPECT_EQ(store.notifications().size(), 1u);
  GithubFetchResult failed;
  failed.error = "boom";
  store.applyFetch(failed);
  EXPECT_TRUE(store.notifications().empty());
  GithubFetchResult good;
  good.ok = true;
  good.notifications = {note("2", "Issue", true), note("3", "Issue", false)};
  store.applyFetch(good);
  EXPECT_EQ(store.unreadCount(), 1);
  store.markRead("2");
  EXPECT_EQ(store.unreadCount(), 0);
}

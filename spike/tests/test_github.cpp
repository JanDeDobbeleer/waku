#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <memory>
#include <vector>

#include "features/github/qml/github_service.h"
#include "features/github/testing/fake_github_client.h"
#include "spike/tests/bar_fixtures.h"

using namespace wabar::github;
using namespace wabar::github::testing;

namespace {

GithubNotification note(const QString& id, const QString& type, bool unread,
                        const QString& title = QStringLiteral("Title")) {
  GithubNotification n;
  n.id = id;
  n.type = type;
  n.unread = unread;
  n.title = title;
  n.repository = QStringLiteral("owner/repo");
  n.url = QStringLiteral("https://github.com/owner/repo/issues/") + id;
  n.updatedAt = QStringLiteral("2026-10-29T10:00:00Z");
  return n;
}

GithubFetchResult okResult(std::vector<GithubNotification> list) {
  GithubFetchResult r;
  r.ok = true;
  r.notifications = std::move(list);
  return r;
}

QByteArray restPage(int count, bool unread = true,
                    const QString& reason = QStringLiteral("mention")) {
  QJsonArray page;
  for (int i = 1; i <= count; ++i) {
    page.append(QJsonObject{
        {QStringLiteral("id"), QString::number(i)},
        {QStringLiteral("unread"), unread},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("updated_at"), QStringLiteral("2026-10-29T10:00:00Z")},
        {QStringLiteral("repository"),
         QJsonObject{{QStringLiteral("full_name"), QStringLiteral("o/r")},
                     {QStringLiteral("html_url"), QStringLiteral("https://github.com/o/r")}}},
        {QStringLiteral("subject"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("Release")},
             {QStringLiteral("title"), QStringLiteral("v") + QString::number(i)},
             {QStringLiteral("url"), QStringLiteral("https://api.github.com/repos/o/r/releases/") +
                                         QString::number(i)}}}});
  }
  return QJsonDocument(page).toJson(QJsonDocument::Compact);
}

struct ServiceHarness {
  explicit ServiceHarness(const QString& token = QStringLiteral("test-token")) {
    auto fake = std::make_unique<FakeClient>();
    client = fake.get();
    client->token = token;
    github = std::make_unique<GitHub>(std::move(fake), nullptr);
  }
  void settle() { QCoreApplication::processEvents(); }
  FakeClient* client = nullptr;
  std::unique_ptr<GitHub> github;
};

}  // namespace

TEST(GithubService, WithoutAClientIsUnavailable) {
  GitHub none(nullptr, nullptr);
  EXPECT_FALSE(none.available());
  EXPECT_FALSE(none.signedIn());
  none.refresh();
  none.signIn();
  none.markAllRead();
  EXPECT_TRUE(none.open(QStringLiteral("1")).isEmpty());
  EXPECT_FALSE(none.signingIn());
  EXPECT_TRUE(none.items().isEmpty());
}

TEST(GithubService, WithoutATokenSendsNothingAndIsSignedOut) {
  ServiceHarness h{QString()};
  h.settle();
  h.github->refresh();
  EXPECT_TRUE(h.github->available());
  EXPECT_FALSE(h.github->signedIn());
  EXPECT_TRUE(h.client->fetches.empty());
  EXPECT_TRUE(h.client->sent.empty());
  EXPECT_TRUE(h.github->error().isEmpty());
  EXPECT_EQ(h.github->unreadCount(), 0);
}

TEST(GithubService, PicksUpATokenSavedLater) {
  ServiceHarness h{QString()};
  h.settle();
  h.client->token = QStringLiteral("test-token");
  h.client->canned = okResult({note(QStringLiteral("1"), QStringLiteral("Issue"), true)});
  h.github->refresh();
  EXPECT_TRUE(h.github->signedIn());
  EXPECT_EQ(h.github->unreadCount(), 1);
}

TEST(GithubService, PublishesItemsAndCounts) {
  ServiceHarness h;
  auto issue = note(QStringLiteral("1"), QStringLiteral("Issue"), true, QStringLiteral("Bug"));
  issue.commentCount = 4;
  issue.issueState = QStringLiteral("closed");
  h.client->canned =
      okResult({issue, note(QStringLiteral("2"), QStringLiteral("PullRequest"), false)});
  QSignalSpy changed(h.github.get(), &GitHub::changed);
  h.settle();
  EXPECT_GE(changed.count(), 1);
  EXPECT_TRUE(h.github->signedIn());
  EXPECT_EQ(h.github->unreadCount(), 1);
  ASSERT_EQ(h.github->items().size(), 2);
  const QVariantMap first = h.github->items().at(0).toMap();
  EXPECT_EQ(first.value(QStringLiteral("id")).toString(), QStringLiteral("1"));
  EXPECT_EQ(first.value(QStringLiteral("title")).toString(), QStringLiteral("Bug"));
  EXPECT_EQ(first.value(QStringLiteral("repo")).toString(), QStringLiteral("owner/repo"));
  EXPECT_EQ(first.value(QStringLiteral("type")).toString(), QStringLiteral("Issue"));
  EXPECT_EQ(first.value(QStringLiteral("category")).toString(), QStringLiteral("Issue"));
  EXPECT_EQ(first.value(QStringLiteral("categoryTitle")).toString(), QStringLiteral("Issues"));
  EXPECT_TRUE(first.value(QStringLiteral("unread")).toBool());
  EXPECT_EQ(first.value(QStringLiteral("comments")).toInt(), 4);
  EXPECT_EQ(first.value(QStringLiteral("state")).toString(), QStringLiteral("closed"));
  EXPECT_FALSE(first.value(QStringLiteral("icon")).toString().isEmpty());
  EXPECT_FALSE(first.value(QStringLiteral("time")).toString().isEmpty());
  EXPECT_EQ(h.github->items().at(1).toMap().value(QStringLiteral("comments")).toInt(), -1);
  EXPECT_TRUE(h.github->error().isEmpty());
}

TEST(GithubService, GroupsFollowTheConfiguredCategoryOrder) {
  ServiceHarness h;
  h.client->canned = okResult({note(QStringLiteral("1"), QStringLiteral("Discussion"), true),
                               note(QStringLiteral("2"), QStringLiteral("PullRequest"), true),
                               note(QStringLiteral("3"), QStringLiteral("Issue"), true),
                               note(QStringLiteral("4"), QStringLiteral("PullRequest"), true),
                               note(QStringLiteral("5"), QStringLiteral("Gist"), true)});
  h.github->setCategoriesOrder({QStringLiteral("Issue"), QStringLiteral("PullRequest")});
  h.settle();
  const QVariantList groups = h.github->groups();
  ASSERT_EQ(groups.size(), 4);
  const auto type = [&](int i) {
    return groups.at(i).toMap().value(QStringLiteral("type")).toString();
  };
  EXPECT_EQ(type(0), QStringLiteral("Issue"));
  EXPECT_EQ(type(1), QStringLiteral("PullRequest"));
  EXPECT_EQ(type(2), QStringLiteral("Discussion"));  // the rest in first-seen order
  EXPECT_EQ(type(3), QStringLiteral("Gist"));
  EXPECT_EQ(groups.at(1).toMap().value(QStringLiteral("title")).toString(),
            QStringLiteral("Pull Requests"));
  EXPECT_EQ(groups.at(1).toMap().value(QStringLiteral("items")).toList().size(), 2);

  QSignalSpy changed(h.github.get(), &GitHub::changed);
  h.github->setCategoriesOrder({QStringLiteral("Gist")});
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(h.github->groups().at(0).toMap().value(QStringLiteral("type")).toString(),
            QStringLiteral("Gist"));
}

TEST(GithubService, OptionsReachTheFetchAndCoalesceIntoOneRefresh) {
  ServiceHarness h;
  h.github->setOnlyUnread(false);
  h.github->setMaxNotification(7);
  h.github->setShowComments(true);
  h.github->setReasonFilters({QStringLiteral("mention")});
  h.github->setInterval(60000);
  h.settle();
  ASSERT_EQ(h.client->fetches.size(), 1u);
  const auto& options = h.client->fetches.front();
  EXPECT_FALSE(options.onlyUnread);
  EXPECT_EQ(options.maxNotification, 7);
  EXPECT_TRUE(options.showCommentCount);
  EXPECT_EQ(options.reasonFilters, QStringList{QStringLiteral("mention")});
  EXPECT_EQ(options.token, QStringLiteral("test-token"));
  EXPECT_EQ(h.github->interval(), 60000);
  h.github->setInterval(10);  // clamped
  EXPECT_EQ(h.github->interval(), 1000);
}

TEST(GithubService, ParsesTheApiAndAppliesTheUnreadFilterAndTheMaximum) {
  ServiceHarness h;
  h.client->useTransport = true;
  h.client->transport.handler = [](const HttpRequest&) {
    return HttpResponse{200, QStringLiteral("OK"), restPage(3)};
  };
  h.github->setMaxNotification(2);
  h.settle();
  ASSERT_FALSE(h.client->transport.requests.empty());
  const HttpRequest& first = h.client->transport.requests.front();
  EXPECT_TRUE(first.url.contains(QStringLiteral("all=false")));  // only unread
  EXPECT_TRUE(first.url.contains(QStringLiteral("per_page=2")));
  ASSERT_EQ(h.github->items().size(), 2);  // the third page entry is cut
  const QVariantMap item = h.github->items().at(0).toMap();
  EXPECT_EQ(item.value(QStringLiteral("type")).toString(), QStringLiteral("Release"));
  EXPECT_EQ(item.value(QStringLiteral("repo")).toString(), QStringLiteral("o/r"));
  EXPECT_EQ(item.value(QStringLiteral("url")).toString(),
            QStringLiteral("https://github.com/o/r/releases"));

  h.github->setOnlyUnread(false);
  h.settle();
  bool sawAll = false;
  for (const auto& request : h.client->transport.requests) {
    sawAll = sawAll || request.url.contains(QStringLiteral("all=true"));
  }
  EXPECT_TRUE(sawAll);
}

TEST(GithubService, ReasonFilterDropsOtherReasons) {
  ServiceHarness h;
  h.client->useTransport = true;
  h.client->transport.handler = [](const HttpRequest&) {
    return HttpResponse{200, QStringLiteral("OK"), restPage(2, true, QStringLiteral("subscribed"))};
  };
  h.github->setReasonFilters({QStringLiteral("mention")});
  h.settle();
  EXPECT_TRUE(h.github->items().isEmpty());
  EXPECT_TRUE(h.github->error().isEmpty());
}

TEST(GithubService, FailuresSetTheErrorAndClearTheList) {
  ServiceHarness h;
  h.client->canned = okResult({note(QStringLiteral("1"), QStringLiteral("Issue"), true)});
  h.settle();
  ASSERT_EQ(h.github->items().size(), 1);

  h.client->useTransport = true;
  h.client->transport.handler = [](const HttpRequest&) {
    return HttpResponse{401, QStringLiteral("Unauthorized")};
  };
  h.github->refresh();
  EXPECT_TRUE(h.github->items().isEmpty());
  EXPECT_EQ(h.github->unreadCount(), 0);
  EXPECT_TRUE(h.github->error().contains(QStringLiteral("401")));

  h.client->transport.handler = [](const HttpRequest&) {
    return HttpResponse{};  // status 0: offline
  };
  h.github->refresh();
  EXPECT_TRUE(h.github->error().contains(QStringLiteral("no internet")));

  h.client->transport.handler = [](const HttpRequest&) {
    return HttpResponse{200, QStringLiteral("OK"), QByteArrayLiteral("{not json")};
  };
  h.github->refresh();
  EXPECT_FALSE(h.github->error().isEmpty());

  h.client->useTransport = false;
  h.github->refresh();  // recovers
  EXPECT_TRUE(h.github->error().isEmpty());
  EXPECT_EQ(h.github->items().size(), 1);
}

TEST(GithubService, AFetchInProgressIsNotDuplicatedButRunsOnceMore) {
  ServiceHarness h;
  h.client->hold = true;
  h.settle();
  ASSERT_EQ(h.client->fetches.size(), 1u);
  EXPECT_TRUE(h.github->loading());
  h.github->refresh();
  h.github->refresh();
  EXPECT_EQ(h.client->fetches.size(), 1u);

  auto done = std::move(h.client->pending);
  h.client->pending = nullptr;
  done(okResult({note(QStringLiteral("1"), QStringLiteral("Issue"), true)}));
  EXPECT_EQ(h.client->fetches.size(), 2u);  // exactly one rerun
  EXPECT_EQ(h.github->items().size(), 1);
  h.client->pending(okResult({}));
  EXPECT_FALSE(h.github->loading());
  EXPECT_EQ(h.client->fetches.size(), 2u);
}

TEST(GithubService, OpenMarksReadReturnsTheUrlAndTellsGithub) {
  ServiceHarness h;
  h.client->canned = okResult({note(QStringLiteral("12"), QStringLiteral("Issue"), true),
                               note(QStringLiteral("13"), QStringLiteral("Issue"), false)});
  h.settle();
  ASSERT_EQ(h.github->unreadCount(), 1);

  EXPECT_EQ(h.github->open(QStringLiteral("12")),
            QStringLiteral("https://github.com/owner/repo/issues/12"));
  EXPECT_EQ(h.github->unreadCount(), 0);
  ASSERT_EQ(h.client->sent.size(), 1u);
  EXPECT_TRUE(h.client->sent.front().url.endsWith(QStringLiteral("/12")));
  EXPECT_EQ(h.client->sent.front().verb, QByteArrayLiteral("PATCH"));

  // Already read: the page opens, GitHub is not asked again.
  EXPECT_EQ(h.github->open(QStringLiteral("13")),
            QStringLiteral("https://github.com/owner/repo/issues/13"));
  EXPECT_EQ(h.client->sent.size(), 1u);
  EXPECT_TRUE(h.github->open(QStringLiteral("nope")).isEmpty());
  EXPECT_FALSE(h.github->items().at(0).toMap().value(QStringLiteral("unread")).toBool());
}

TEST(GithubService, MarkAllReadClearsTheCountAndTellsGithubOnce) {
  ServiceHarness h;
  h.client->canned = okResult({note(QStringLiteral("1"), QStringLiteral("Issue"), true),
                               note(QStringLiteral("2"), QStringLiteral("Release"), true)});
  h.settle();
  ASSERT_EQ(h.github->unreadCount(), 2);
  QSignalSpy changed(h.github.get(), &GitHub::changed);
  h.github->markAllRead();
  EXPECT_EQ(h.github->unreadCount(), 0);
  EXPECT_EQ(changed.count(), 1);
  ASSERT_EQ(h.client->sent.size(), 1u);
  EXPECT_EQ(h.client->sent.front().verb, QByteArrayLiteral("PUT"));
  EXPECT_EQ(h.github->items().size(), 2);  // still listed, now read
}

TEST(GithubService, SignInShowsTheCodeThenSavesTheTokenAndFetches) {
  ServiceHarness h{QString()};
  h.settle();
  h.client->canned = okResult({note(QStringLiteral("1"), QStringLiteral("Issue"), true)});
  int step = 0;
  h.client->respond = [&step](const HttpRequest& request) {
    if (request.url.endsWith(QStringLiteral("/device/code"))) {
      return HttpResponse{
          200, QStringLiteral("OK"),
          QByteArrayLiteral("{\"device_code\":\"dc\",\"user_code\":\"ABCD-1234\","
                            "\"verification_uri\":\"https://github.com/login/device\","
                            "\"interval\":5,\"expires_in\":900}")};
    }
    ++step;
    return HttpResponse{200, QStringLiteral("OK"),
                        step == 1 ? QByteArrayLiteral("{\"error\":\"authorization_pending\"}")
                                  : QByteArrayLiteral("{\"access_token\":\"made-up-token\"}")};
  };
  h.github->signIn();
  EXPECT_TRUE(h.github->signingIn());
  EXPECT_EQ(h.github->userCode(), QStringLiteral("ABCD-1234"));
  EXPECT_EQ(h.github->verificationUri(), QStringLiteral("https://github.com/login/device"));
  EXPECT_FALSE(h.github->signedIn());

  h.github->signIn();  // already running: no second request
  EXPECT_EQ(h.client->sent.size(), 1u);

  h.github->pollSignIn();  // pending
  EXPECT_TRUE(h.github->signingIn());
  EXPECT_FALSE(h.github->signedIn());
  h.github->pollSignIn();  // approved
  EXPECT_FALSE(h.github->signingIn());
  EXPECT_TRUE(h.github->userCode().isEmpty());
  EXPECT_TRUE(h.github->signedIn());
  EXPECT_EQ(h.client->saved, QStringLiteral("made-up-token"));
  EXPECT_TRUE(h.github->signInError().isEmpty());
  ASSERT_EQ(h.client->fetches.size(), 1u);
  EXPECT_EQ(h.client->fetches.front().token, QStringLiteral("made-up-token"));
  EXPECT_EQ(h.github->unreadCount(), 1);
}

TEST(GithubService, SignInReportsFailuresAndCanBeCancelled) {
  ServiceHarness h{QString()};
  h.settle();
  h.client->respond = [](const HttpRequest&) {
    return HttpResponse{};  // offline
  };
  h.github->signIn();
  EXPECT_FALSE(h.github->signingIn());
  EXPECT_FALSE(h.github->signInError().isEmpty());

  h.client->respond = [](const HttpRequest& request) {
    if (request.url.endsWith(QStringLiteral("/device/code"))) {
      return HttpResponse{200, QStringLiteral("OK"),
                          QByteArrayLiteral("{\"device_code\":\"dc\",\"user_code\":\"X\"}")};
    }
    return HttpResponse{200, QStringLiteral("OK"),
                        QByteArrayLiteral("{\"error\":\"access_denied\"}")};
  };
  h.github->signIn();
  EXPECT_TRUE(h.github->signingIn());
  EXPECT_EQ(h.github->verificationUri(), QStringLiteral("https://github.com/login/device"));
  h.github->pollSignIn();
  EXPECT_FALSE(h.github->signingIn());
  EXPECT_EQ(h.github->signInError(), QStringLiteral("Authorization was denied."));
  EXPECT_FALSE(h.github->signedIn());
  EXPECT_TRUE(h.client->saved.isEmpty());

  h.github->signIn();
  ASSERT_TRUE(h.github->signingIn());
  h.github->cancelSignIn();
  EXPECT_FALSE(h.github->signingIn());
  EXPECT_TRUE(h.github->signInError().isEmpty());
  h.github->pollSignIn();  // nothing left to poll
  EXPECT_FALSE(h.github->signedIn());
}

TEST(GithubService, ATokenThatCannotBeSavedStillSignsIn) {
  ServiceHarness h{QString()};
  h.settle();
  h.client->saveOk = false;
  h.client->respond = [](const HttpRequest& request) {
    const bool device = request.url.endsWith(QStringLiteral("/device/code"));
    return HttpResponse{200, QStringLiteral("OK"),
                        device ? QByteArrayLiteral("{\"device_code\":\"dc\",\"user_code\":\"X\"}")
                               : QByteArrayLiteral("{\"access_token\":\"made-up-token\"}")};
  };
  h.github->signIn();
  h.github->pollSignIn();
  EXPECT_TRUE(h.github->signedIn());
  EXPECT_FALSE(h.github->signInError().isEmpty());
}

TEST(GithubService, TruncateMatchesPython) {
  ServiceHarness h;
  EXPECT_EQ(h.github->truncate(QStringLiteral("short"), 10), QStringLiteral("short"));
  EXPECT_EQ(h.github->truncate(QStringLiteral("0123456789"), 8), QStringLiteral("01234..."));
}

// The widget itself, on the offscreen platform: the real service has no client there, so the pill
// is hidden, nothing is fetched and the QML still loads and binds its options.
TEST(GithubWidget, LoadsAndIsHiddenWithoutTheService) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
Config {
    Bar {
        layout: BarLayout {
            start: [ GitHub { objectName: "gh"; maxNotification: 12; categoriesOrder: ["Release"] } ]
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  QQuickWindow* window = h.window();
  ASSERT_TRUE(window && QTest::qWaitForWindowExposed(window));
  QQuickItem* gh = findItem(window, QStringLiteral("gh"));
  ASSERT_NE(gh, nullptr);
  EXPECT_FALSE(gh->isVisible());
  EXPECT_NE(findItem(window, QStringLiteral("githubDot")), nullptr);
  EXPECT_NE(findWindow(QStringLiteral("githubPopup")), nullptr);
}

#include <gtest/gtest.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <nlohmann/json.hpp>

#include "platform/github_api.h"
#include "widgets/github/github_format.h"
#include "widgets/github/github_icons.h"

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

const QDateTime kNow = QDateTime(QDate(2026, 10, 28), QTime(12, 0), QTimeZone::UTC);

}  // namespace

// --- pure helpers ------------------------------------------------------------------------------

TEST(GithubFormat, RelativeTimeMatchesPythonBuckets) {
  using yasb::widgets::github::relativeTime;
  const auto at = [](const char* iso) { return relativeTime(QString(iso), kNow); };
  EXPECT_EQ(at("2026-10-28T11:59:30Z"), "just now");
  EXPECT_EQ(at("2026-10-28T11:59:00Z"), "1 minute ago");
  EXPECT_EQ(at("2026-10-28T11:15:00Z"), "45 minutes ago");
  EXPECT_EQ(at("2026-10-28T10:00:00Z"), "2 hours ago");
  EXPECT_EQ(at("2026-10-27T12:00:00Z"), "1 day ago");
  EXPECT_EQ(at("2026-10-22T12:00:00Z"), "6 days ago");
  EXPECT_EQ(at("2026-10-21T12:00:00Z"), "1 week ago");
  EXPECT_EQ(at("2026-10-01T12:00:00Z"), "3 weeks ago");
  EXPECT_EQ(at("2026-09-28T12:00:00Z"), "1 month ago");
  EXPECT_EQ(at("2025-10-27T12:00:00Z"), "1 year ago");
  EXPECT_EQ(at(""), "");
  EXPECT_EQ(at("not a date"), "");
}

TEST(GithubFormat, CategoryTitlesAndTruncation) {
  using namespace yasb::widgets::github;
  EXPECT_EQ(categoryTitle("PullRequest"), "Pull Requests");
  EXPECT_EQ(categoryTitle("CheckSuite"), "Check Suites");
  EXPECT_EQ(categoryTitle("RepositoryVulnerabilityAlert"), "Repository Vulnerability Alert");
  EXPECT_EQ(categoryTitle("Commit"), "Commit");
  EXPECT_EQ(truncateField("short", 10), "short");
  EXPECT_EQ(truncateField("exactly10!", 10), "exactly10!");
  EXPECT_EQ(truncateField("this is longer", 10), "this is...");
}

TEST(GithubFormat, IconsAndStatesFollowPythonPriority) {
  yasb::widgets::github::GithubIcons icons;
  icons.issue = "I";
  icons.issue_closed = "IC";
  icons.pull_request = "P";
  icons.pull_request_closed = "PC";
  icons.pull_request_merged = "PM";
  icons.pull_request_draft = "PD";
  icons.discussion = "D";
  icons.discussion_answered = "DA";
  icons.release = "R";
  icons.checksuite = "CS";
  icons.default_ = "X";
  using namespace yasb::widgets::github;
  GithubNotification issue = note("1", "Issue", true);
  EXPECT_EQ(iconAndStates(issue, icons).icon, "I");
  issue.issueState = QString("closed");
  EXPECT_EQ(iconAndStates(issue, icons).icon, "IC");
  EXPECT_EQ(iconAndStates(issue, icons).states, QStringList{"closed"});

  GithubNotification pr = note("2", "PullRequest", true);
  pr.pullRequestState = QString("open");
  pr.pullRequestIsDraft = true;
  EXPECT_EQ(iconAndStates(pr, icons).icon, "PD");
  EXPECT_EQ(iconAndStates(pr, icons).states, (QStringList{"draft", "open"}));
  pr.pullRequestState = QString("closed");
  EXPECT_EQ(iconAndStates(pr, icons).icon, "PC");
  pr.pullRequestIsMerged = true;
  EXPECT_EQ(iconAndStates(pr, icons).icon, "PM");
  EXPECT_EQ(iconAndStates(pr, icons).states, (QStringList{"merged", "closed"}));

  GithubNotification discussion = note("3", "Discussion", true);
  discussion.discussionIsAnswered = true;
  EXPECT_EQ(iconAndStates(discussion, icons).icon, "DA");
  EXPECT_EQ(iconAndStates(note("4", "Release", true), icons).icon, "R");
  EXPECT_EQ(iconAndStates(note("5", "CheckSuite", true), icons).icon, "CS");
  EXPECT_EQ(iconAndStates(note("6", "Commit", true), icons).icon, "X");
}

// --- API layer ---------------------------------------------------------------------------------

TEST(GithubApi, ParsesNotificationsAndBuildsWebUrls) {
  const auto page = parseGithubNotifications(pageOf({
      notificationJson("1", "PullRequest", "https://api.github.com/repos/o/r/pulls/5", "o/r", true),
      notificationJson("2", "Issue", "https://api.github.com/repos/o/r/issues/6", "o/r", false),
      notificationJson("3", "Release", "https://api.github.com/repos/o/r/releases/7", "o/r", true),
      notificationJson("4", "CheckSuite", "https://api.github.com/repos/o/r/check-suites/8", "o/r",
                       true),
      notificationJson("5", "Commit", "https://api.github.com/repos/o/r/commits/abc", "o/r", true),
  }));
  ASSERT_TRUE(page.has_value());
  ASSERT_EQ(page->size(), 5u);
  EXPECT_EQ((*page)[0].url, "https://github.com/o/r/pull/5");
  EXPECT_EQ((*page)[1].url, "https://github.com/o/r/issues/6");
  EXPECT_EQ((*page)[2].url, "https://github.com/o/r/releases");
  EXPECT_EQ((*page)[3].url, "https://github.com/o/r/actions");
  EXPECT_EQ((*page)[4].url, "https://github.com/o/r");
  EXPECT_TRUE((*page)[0].unread);
  EXPECT_FALSE((*page)[1].unread);
  EXPECT_EQ((*page)[0].reason, "mention");
  EXPECT_FALSE(parseGithubNotifications("{}").has_value());
  EXPECT_FALSE(parseGithubNotifications("[{\"id\":\"1\"}]").has_value());
}

TEST(GithubApi, ParsesLinkHeaderAndSubjectUrls) {
  const auto links = parseGithubLinkHeader(
      "<https://api.github.com/notifications?page=2>; rel=\"next\", "
      "<https://api.github.com/notifications?page=9>; rel=\"last\"");
  EXPECT_EQ(links.at("next"), "https://api.github.com/notifications?page=2");
  EXPECT_EQ(links.at("last"), "https://api.github.com/notifications?page=9");
  const auto ref = parseGithubSubjectUrl("https://api.github.com/repos/octo/hello/issues/12/");
  ASSERT_TRUE(ref.has_value());
  EXPECT_EQ(ref->owner, "octo");
  EXPECT_EQ(ref->repo, "hello");
  EXPECT_EQ(ref->number, 12);
  EXPECT_FALSE(parseGithubSubjectUrl("https://api.github.com/repos/octo/hello").has_value());
  EXPECT_FALSE(parseGithubSubjectUrl("https://api.github.com/repos/o/r/issues/abc").has_value());
  EXPECT_EQ(githubNotificationsUrl(true, 30),
            "https://api.github.com/notifications?all=false&participating=false&per_page=30");
  EXPECT_EQ(githubNotificationsUrl(false, 200),
            "https://api.github.com/notifications?all=true&participating=false&per_page=50");
}

TEST(GithubApi, EnrichmentQueryAndResponse) {
  auto page = parseGithubNotifications(pageOf({
      notificationJson("1", "Issue", "https://api.github.com/repos/o/r/issues/6", "o/r", true),
      notificationJson("2", "Release", "https://api.github.com/repos/o/r/releases/7", "o/r", true),
      notificationJson("3", "PullRequest", "https://api.github.com/repos/o/r/pulls/5", "o/r", true),
      notificationJson("4", "Discussion", "https://api.github.com/repos/o/r/discussions/9", "o/r",
                       true),
  }));
  ASSERT_TRUE(page.has_value());
  const QString query = githubEnrichmentQuery(*page, true);
  EXPECT_EQ(query,
            "query {\n"
            "n0: repository(owner: \"o\", name: \"r\") { issue(number: 6) { state comments { "
            "totalCount } } }\n"
            "n2: repository(owner: \"o\", name: \"r\") { pullRequest(number: 5) { state mergedAt "
            "isDraft comments { totalCount } reviewThreads { totalCount } } }\n"
            "n3: repository(owner: \"o\", name: \"r\") { discussion(number: 9) { isAnswered "
            "comments { totalCount } } }}");
  EXPECT_TRUE(githubEnrichmentQuery(*page, false).contains("{ state } }"));

  const QByteArray response = R"({"data":{
    "n0":{"issue":{"state":"CLOSED","comments":{"totalCount":4}}},
    "n2":{"pullRequest":{"state":"MERGED","mergedAt":"2026-10-01T00:00:00Z","isDraft":false,
          "comments":{"totalCount":2},"reviewThreads":{"totalCount":3}}},
    "n3":{"discussion":{"isAnswered":true,"comments":{"totalCount":1}}}}})";
  ASSERT_TRUE(applyGithubEnrichment(response, *page, true));
  EXPECT_EQ((*page)[0].issueState, QString("closed"));
  EXPECT_EQ((*page)[0].commentCount, 4);
  EXPECT_FALSE((*page)[1].commentCount.has_value());
  EXPECT_EQ((*page)[2].pullRequestState, QString("merged"));
  EXPECT_TRUE((*page)[2].pullRequestIsMerged);
  EXPECT_EQ((*page)[2].pullRequestIsDraft, std::optional<bool>(false));
  EXPECT_EQ((*page)[2].commentCount, 5);
  EXPECT_EQ((*page)[3].discussionIsAnswered, std::optional<bool>(true));
  EXPECT_EQ((*page)[3].commentCount, 1);

  auto untouched = *page;
  EXPECT_FALSE(applyGithubEnrichment(R"({"errors":[{"message":"x"}]})", untouched, true));
}

TEST(GithubApi, FetchFollowsPagesFiltersReasonsAndEnriches) {
  FakeTransport transport;
  transport.add(
      "notifications?page=2",
      ok(pageOf({notificationJson("3", "Issue", "https://api.github.com/repos/o/r/issues/3", "o/r",
                                  true, "subscribed")})));
  transport.add(
      "per_page=2",
      ok(pageOf({notificationJson("1", "Issue", "https://api.github.com/repos/o/r/issues/1", "o/r",
                                  true, "mention"),
                 notificationJson("2", "Issue", "https://api.github.com/repos/o/r/issues/2", "o/r",
                                  true, "Review_Requested")}),
         "<https://api.github.com/notifications?page=2>; rel=\"next\""));
  transport.add(
      "/graphql",
      ok(R"({"data":{"n0":{"issue":{"state":"OPEN"}},"n1":{"issue":{"state":"CLOSED"}}}})"),
      "POST");
  GithubFetchOptions options;
  options.token = "fake";
  options.maxNotification = 2;
  options.reasonFilters = {"MENTION", "review_requested"};
  const GithubFetchResult result = fetchGithubNotifications(transport, options);
  ASSERT_TRUE(result.ok) << result.error.toStdString();
  // max_notification trims the second page away.
  ASSERT_EQ(result.notifications.size(), 2u);
  EXPECT_EQ(result.notifications[0].issueState, QString("open"));
  EXPECT_EQ(result.notifications[1].issueState, QString("closed"));
  EXPECT_TRUE(result.notifications[0].subjectApiUrl.isEmpty());
  ASSERT_EQ(transport.requests.size(), 2u);
  EXPECT_EQ(transport.requests[0].headers[0].second, QByteArray("token fake"));
  EXPECT_EQ(transport.requests[1].verb, QByteArray("POST"));
  EXPECT_EQ(transport.requests[1].headers[0].second, QByteArray("bearer fake"));
}

TEST(GithubApi, FetchErrorsReturnNotOk) {
  {
    FakeTransport transport;
    GithubFetchOptions options;
    options.token = "fake";
    const auto result = fetchGithubNotifications(transport, options);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.isEmpty());
  }
  {
    FakeTransport transport;
    HttpResponse unauthorized;
    unauthorized.status = 401;
    unauthorized.reason = "Unauthorized";
    transport.add("/notifications", unauthorized);
    GithubFetchOptions options;
    options.token = "fake";
    const auto result = fetchGithubNotifications(transport, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("401"));
  }
}

TEST(GithubApi, GraphQlFailureKeepsRestData) {
  FakeTransport transport;
  transport.add("/notifications",
                ok(pageOf({notificationJson(
                    "1", "Issue", "https://api.github.com/repos/o/r/issues/1", "o/r", true)})));
  HttpResponse bad;
  bad.status = 502;
  transport.add("/graphql", bad, "POST");
  GithubFetchOptions options;
  options.token = "fake";
  const auto result = fetchGithubNotifications(transport, options);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.notifications.size(), 1u);
  EXPECT_FALSE(result.notifications[0].issueState.has_value());
}

TEST(GithubApi, MarkReadRequestsMatchPython) {
  const HttpRequest one = githubMarkThreadReadRequest("tok", "99");
  EXPECT_EQ(one.verb, QByteArray("PATCH"));
  EXPECT_EQ(one.url, "https://api.github.com/notifications/threads/99");
  EXPECT_EQ(one.headers[0].second, QByteArray("token tok"));
  const HttpRequest all = githubMarkAllReadRequest("tok", kNow);
  EXPECT_EQ(all.verb, QByteArray("PUT"));
  EXPECT_EQ(all.url, "https://api.github.com/notifications");
  EXPECT_EQ(all.body, QByteArray("{\"last_read_at\": \"2026-10-28T12:00:00Z\"}"));
}

TEST(GithubApi, TokenResolutionOrderAndFile) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString file = dir.filePath("YASB/github_token");
  EXPECT_EQ(resolveGithubToken("", "", file), "");
  ASSERT_TRUE(saveGithubTokenFile(file, "from-file"));
  EXPECT_EQ(readGithubTokenFile(file), "from-file");
  EXPECT_EQ(resolveGithubToken("", "", file), "from-file");
  EXPECT_EQ(resolveGithubToken("configured", "env-token", file), "configured");
  EXPECT_EQ(resolveGithubToken("env", "env-token", file), "env-token");
  // "env" with the variable unset falls back to the saved token.
  EXPECT_EQ(resolveGithubToken("env", "", file), "from-file");
  QFile raw(file);
  ASSERT_TRUE(raw.open(QIODevice::WriteOnly));
  raw.write("  padded\r\n");
  raw.close();
  EXPECT_EQ(readGithubTokenFile(file), "padded");
}

TEST(GithubApi, DeviceFlowRequestsAndResponses) {
  const HttpRequest code = githubDeviceCodeRequest();
  EXPECT_EQ(code.url, "https://github.com/login/device/code");
  EXPECT_EQ(code.body, QByteArray("client_id=Ov23li0KAXuxNzbEl9Jy&scope=notifications"));
  const HttpRequest poll = githubAccessTokenRequest("dev code");
  EXPECT_TRUE(poll.body.contains("device_code=dev%20code"));
  EXPECT_TRUE(
      poll.body.contains("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"));

  const GithubDeviceCode parsed = parseGithubDeviceCode(ok(
      R"({"device_code":"d","user_code":"AB-12","verification_uri":"https://github.com/login/device","interval":7,"expires_in":900})"));
  EXPECT_TRUE(parsed.error.isEmpty());
  EXPECT_EQ(parsed.userCode, "AB-12");
  EXPECT_EQ(parsed.interval, 7);
  EXPECT_EQ(parseGithubDeviceCode(ok(R"({"error":"x","error_description":"nope"})")).error, "nope");
  EXPECT_FALSE(parseGithubDeviceCode(HttpResponse{}).error.isEmpty());

  using Kind = GithubTokenPoll::Kind;
  EXPECT_EQ(parseGithubAccessToken(ok(R"({"error":"authorization_pending"})")).kind, Kind::Pending);
  EXPECT_EQ(parseGithubAccessToken(ok(R"({"error":"slow_down"})")).kind, Kind::SlowDown);
  EXPECT_EQ(parseGithubAccessToken(ok(R"({"error":"access_denied"})")).message,
            "Authorization was denied.");
  EXPECT_EQ(parseGithubAccessToken(ok(R"({"error":"expired_token"})")).kind, Kind::Error);
  EXPECT_EQ(parseGithubAccessToken(ok(R"({"error":"weird"})")).message,
            "Authorization failed: weird");
  const auto success = parseGithubAccessToken(ok(R"({"access_token":"gho_fake"})"));
  EXPECT_EQ(success.kind, Kind::Token);
  EXPECT_EQ(success.token, "gho_fake");
}

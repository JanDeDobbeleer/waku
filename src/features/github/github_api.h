#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// Port of yasb:src/core/widgets/services/github/{api,auth}.py. Never log or persist the token
// anywhere except the token file the Python build also uses.
namespace wabar::github {

struct GithubNotification {
  QString id;
  QString repository;
  QString title;
  QString type;
  QString url;
  QString reason;
  QString updatedAt;
  bool unread = false;
  std::optional<int> commentCount;
  // GraphQL enrichment; states are lower-case.
  std::optional<QString> issueState;
  std::optional<QString> pullRequestState;
  bool pullRequestIsMerged = false;
  std::optional<bool> pullRequestIsDraft;
  std::optional<bool> discussionIsAnswered;
  // REST subject URL, used only to build the GraphQL query.
  QString subjectApiUrl;
};

struct HttpRequest {
  QByteArray verb = QByteArrayLiteral("GET");
  QString url;
  QList<QPair<QByteArray, QByteArray>> headers;
  QByteArray body;
};

struct HttpResponse {
  // 0 when no HTTP response arrived (offline, DNS, TLS, timeout).
  int status = 0;
  QString reason;
  QByteArray body;
  QByteArray link;
  QString error;
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  virtual HttpResponse send(const HttpRequest& request) = 0;
};

// Blocking QtNetwork transport: each send() runs a local event loop, so use it from a worker
// thread only.
std::unique_ptr<HttpTransport> createNetworkTransport(int timeoutMs = 15000);

// Runs `request` on a QThreadPool thread with a fresh network transport and delivers the response
// on the GUI thread (posted to qApp). `done` is skipped when `context` was destroyed meanwhile.
void sendHttpAsync(HttpRequest request, QObject* context,
                   std::function<void(const HttpResponse&)> done);

struct GithubFetchOptions {
  QString token;
  bool onlyUnread = true;
  int maxNotification = 50;
  QStringList reasonFilters;
  bool showCommentCount = false;
};

struct GithubFetchResult {
  bool ok = false;
  QString error;
  std::vector<GithubNotification> notifications;
};

QString githubNotificationsUrl(bool onlyUnread, int maxNotification);
std::map<QString, QString> parseGithubLinkHeader(const QString& header);
// (owner, repo, number) from an API subject URL, or nullopt.
struct GithubSubjectRef {
  QString owner;
  QString repo;
  int number = 0;
};
std::optional<GithubSubjectRef> parseGithubSubjectUrl(const QString& subjectUrl);

// Maps one page of the REST notifications API; nullopt when the JSON is not the expected shape.
std::optional<std::vector<GithubNotification>> parseGithubNotifications(const QByteArray& page);

// GraphQL enrichment query for Issue/PullRequest/Discussion items; aliases are "n<index>".
QString githubEnrichmentQuery(const std::vector<GithubNotification>& notifications,
                              bool includeCommentCount);
// Applies a GraphQL response; returns false (and changes nothing) when it carries errors.
bool applyGithubEnrichment(const QByteArray& response,
                           std::vector<GithubNotification>& notifications,
                           bool includeCommentCount);

GithubFetchResult fetchGithubNotifications(HttpTransport& transport,
                                           const GithubFetchOptions& options);

HttpRequest githubMarkThreadReadRequest(const QString& token, const QString& notificationId);
HttpRequest githubMarkAllReadRequest(const QString& token, const QDateTime& nowUtc);

// %LOCALAPPDATA%\wabar\github_token (the wabar data folder; copied once from YASB).
QString githubTokenFilePath();
// GithubWidget token order: "env" -> YASB_GITHUB_TOKEN, else the option; empty -> token file.
QString resolveGithubToken(const QString& configToken, const QString& envToken,
                           const QString& tokenFilePath);
QString readGithubTokenFile(const QString& path);
bool saveGithubTokenFile(const QString& path, const QString& token);

// OAuth device flow for the "notifications" app (auth.py).
struct GithubDeviceCode {
  QString deviceCode;
  QString userCode;
  QString verificationUri;
  int interval = 5;
  int expiresIn = 0;
  QString error;
};

struct GithubTokenPoll {
  enum class Kind { Pending, SlowDown, Token, Error };
  Kind kind = Kind::Error;
  QString token;
  QString message;
};

HttpRequest githubDeviceCodeRequest();
GithubDeviceCode parseGithubDeviceCode(const HttpResponse& response);
HttpRequest githubAccessTokenRequest(const QString& deviceCode);
GithubTokenPoll parseGithubAccessToken(const HttpResponse& response);

}  // namespace wabar::github

Q_DECLARE_METATYPE(wabar::github::GithubFetchResult)

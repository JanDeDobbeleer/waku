#include "features/github/github_api.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>
#include <QtDebug>
#include <exception>
#include <set>

#include "kernel/qt/app_data.h"

namespace wabar::github {
namespace {

const QString kApi = QStringLiteral("https://api.github.com");
const QByteArray kClientId = QByteArrayLiteral("Ov23li0KAXuxNzbEl9Jy");
const QByteArray kScope = QByteArrayLiteral("notifications");
const QString kNetworkError = QStringLiteral("Network error.\nCheck your internet connection.");

class NetworkTransport final : public HttpTransport {
 public:
  explicit NetworkTransport(int timeoutMs) : timeoutMs_(timeoutMs) {}

  HttpResponse send(const HttpRequest& request) override {
    QNetworkAccessManager manager;
    QNetworkRequest networkRequest{QUrl(request.url)};
    networkRequest.setTransferTimeout(timeoutMs_);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("yasb-native"));
    for (const auto& [name, value] : request.headers) networkRequest.setRawHeader(name, value);
    QNetworkReply* reply = manager.sendCustomRequest(networkRequest, request.verb, request.body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished()) loop.exec();
    HttpResponse response;
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    response.body = reply->readAll();
    response.link = reply->rawHeader(QByteArrayLiteral("Link"));
    if (response.status == 0) response.error = reply->errorString();
    delete reply;
    return response;
  }

 private:
  int timeoutMs_;
};

QByteArray authHeader(const QString& token) { return QByteArrayLiteral("token ") + token.toUtf8(); }

bool isSuccess(int status) { return status >= 200 && status < 300; }

QString httpFailure(const HttpResponse& response) {
  return QStringLiteral("%1 - %2").arg(response.status).arg(response.reason);
}

QByteArray formEncode(const QList<QPair<QByteArray, QByteArray>>& fields) {
  QByteArray out;
  for (const auto& [key, value] : fields) {
    if (!out.isEmpty()) out += '&';
    out += QUrl::toPercentEncoding(QString::fromUtf8(key)) + '=' +
           QUrl::toPercentEncoding(QString::fromUtf8(value));
  }
  return out;
}

QString webUrl(const QString& type, const QString& subjectUrl, const QString& repo,
               const QString& htmlUrl) {
  if (type == QLatin1String("PullRequest")) {
    QString url = subjectUrl;
    return url.replace(QStringLiteral("api.github.com/repos"), QStringLiteral("github.com"))
        .replace(QStringLiteral("/pulls/"), QStringLiteral("/pull/"));
  }
  if (type == QLatin1String("Release"))
    return QStringLiteral("https://github.com/%1/releases").arg(repo);
  if (type == QLatin1String("Issue") || type == QLatin1String("Discussion")) {
    QString url = subjectUrl;
    return url.replace(QStringLiteral("api.github.com/repos"), QStringLiteral("github.com"));
  }
  if (type == QLatin1String("CheckSuite"))
    return QStringLiteral("https://github.com/%1/actions").arg(repo);
  return htmlUrl;
}

std::optional<int> totalCount(const QJsonObject& node, const char* key) {
  const QJsonValue value = node.value(QLatin1String(key));
  if (!value.isObject()) return std::nullopt;
  const QJsonValue count = value.toObject().value(QStringLiteral("totalCount"));
  if (!count.isDouble()) return std::nullopt;
  return count.toInt();
}

}  // namespace

std::unique_ptr<HttpTransport> createNetworkTransport(int timeoutMs) {
  return std::make_unique<NetworkTransport>(timeoutMs);
}

void sendHttpAsync(HttpRequest request, QObject* context,
                   std::function<void(const HttpResponse&)> done) {
  const bool hasContext = context != nullptr;
  QPointer<QObject> guard(context);
  QThreadPool::globalInstance()->start(
      [request = std::move(request), guard, hasContext, done = std::move(done)] {
        HttpResponse response;
        try {
          response = createNetworkTransport()->send(request);
        } catch (const std::exception& error) {
          response.error = QString::fromUtf8(error.what());
        }
        QCoreApplication* app = QCoreApplication::instance();
        if (!app || !done) return;
        QMetaObject::invokeMethod(
            app,
            [guard, hasContext, done, response] {
              if (hasContext && !guard) return;
              done(response);
            },
            Qt::QueuedConnection);
      });
}

QString githubNotificationsUrl(bool onlyUnread, int maxNotification) {
  return QStringLiteral("%1/notifications?all=%2&participating=false&per_page=%3")
      .arg(kApi, onlyUnread ? QStringLiteral("false") : QStringLiteral("true"))
      .arg(std::min(maxNotification, 50));
}

std::map<QString, QString> parseGithubLinkHeader(const QString& header) {
  std::map<QString, QString> links;
  for (const QString& part : header.split(QLatin1Char(','))) {
    const QStringList section = part.split(QLatin1Char(';'));
    if (section.size() < 2) continue;
    QString url = section[0].trimmed();
    while (url.startsWith(QLatin1Char('<')) || url.startsWith(QLatin1Char('>'))) url.remove(0, 1);
    while (url.endsWith(QLatin1Char('<')) || url.endsWith(QLatin1Char('>'))) url.chop(1);
    for (qsizetype i = 1; i < section.size(); ++i) {
      const QString param = section[i].trimmed();
      if (param.startsWith(QLatin1String("rel=\"")) && param.endsWith(QLatin1Char('"')) &&
          param.size() >= 6) {
        links[param.mid(5, param.size() - 6)] = url;
      }
    }
  }
  return links;
}

std::optional<GithubSubjectRef> parseGithubSubjectUrl(const QString& subjectUrl) {
  QString cleaned = subjectUrl;
  while (cleaned.endsWith(QLatin1Char('/'))) cleaned.chop(1);
  const QStringList parts = cleaned.split(QLatin1Char('/'));
  if (parts.size() < 8) return std::nullopt;
  bool ok = false;
  const int number = parts.last().split(QLatin1Char('?')).first().trimmed().toInt(&ok);
  if (!ok) return std::nullopt;
  return GithubSubjectRef{parts[4], parts[5], number};
}

std::optional<std::vector<GithubNotification>> parseGithubNotifications(const QByteArray& page) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(page, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isArray()) return std::nullopt;
  std::vector<GithubNotification> result;
  for (const QJsonValue& value : doc.array()) {
    const QJsonObject item = value.toObject();
    const QJsonValue repository = item.value(QStringLiteral("repository"));
    const QJsonValue subject = item.value(QStringLiteral("subject"));
    const QJsonValue id = item.value(QStringLiteral("id"));
    if (!repository.isObject() || !subject.isObject() || id.isUndefined() ||
        !item.contains(QStringLiteral("unread"))) {
      return std::nullopt;
    }
    const QJsonObject repo = repository.toObject();
    const QJsonObject subj = subject.toObject();
    if (!repo.contains(QStringLiteral("full_name")) || !subj.contains(QStringLiteral("type")) ||
        !subj.contains(QStringLiteral("url")) || !subj.contains(QStringLiteral("title"))) {
      return std::nullopt;
    }
    GithubNotification n;
    n.id = id.isString() ? id.toString() : QString::number(id.toInteger());
    n.repository = repo.value(QStringLiteral("full_name")).toString();
    n.title = subj.value(QStringLiteral("title")).toString();
    n.type = subj.value(QStringLiteral("type")).toString();
    n.subjectApiUrl = subj.value(QStringLiteral("url")).toString();
    n.url = webUrl(n.type, n.subjectApiUrl, n.repository,
                   repo.value(QStringLiteral("html_url")).toString());
    n.unread = item.value(QStringLiteral("unread")).toBool();
    n.reason = item.value(QStringLiteral("reason")).toString();
    n.updatedAt = item.value(QStringLiteral("updated_at")).toString();
    result.push_back(std::move(n));
  }
  return result;
}

QString githubEnrichmentQuery(const std::vector<GithubNotification>& notifications,
                              bool includeCommentCount) {
  QStringList parts;
  for (size_t index = 0; index < notifications.size(); ++index) {
    const GithubNotification& n = notifications[index];
    if (n.subjectApiUrl.isEmpty()) continue;
    QString node;
    QString fields;
    if (n.type == QLatin1String("Issue")) {
      node = QStringLiteral("issue");
      fields = includeCommentCount ? QStringLiteral("state comments { totalCount }")
                                   : QStringLiteral("state");
    } else if (n.type == QLatin1String("PullRequest")) {
      node = QStringLiteral("pullRequest");
      fields = includeCommentCount
                   ? QStringLiteral(
                         "state mergedAt isDraft comments { totalCount } reviewThreads { "
                         "totalCount }")
                   : QStringLiteral("state mergedAt isDraft");
    } else if (n.type == QLatin1String("Discussion")) {
      node = QStringLiteral("discussion");
      fields = includeCommentCount ? QStringLiteral("isAnswered comments { totalCount }")
                                   : QStringLiteral("isAnswered");
    } else {
      continue;
    }
    const auto ref = parseGithubSubjectUrl(n.subjectApiUrl);
    if (!ref) continue;
    parts << QStringLiteral(
                 "n%1: repository(owner: \"%2\", name: \"%3\") { %4(number: %5) { %6 } }")
                 .arg(index)
                 .arg(ref->owner, ref->repo, node)
                 .arg(ref->number)
                 .arg(fields);
  }
  if (parts.isEmpty()) return {};
  return QStringLiteral("query {\n") + parts.join(QLatin1Char('\n')) + QLatin1Char('}');
}

bool applyGithubEnrichment(const QByteArray& response,
                           std::vector<GithubNotification>& notifications,
                           bool includeCommentCount) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return false;
  const QJsonObject root = doc.object();
  const QJsonValue errors = root.value(QStringLiteral("errors"));
  if ((errors.isArray() && !errors.toArray().isEmpty()) ||
      (errors.isObject() && !errors.toObject().isEmpty()) ||
      (errors.isString() && !errors.toString().isEmpty()) || errors.toBool()) {
    return false;
  }
  const QJsonObject data = root.value(QStringLiteral("data")).toObject();
  for (size_t index = 0; index < notifications.size(); ++index) {
    const QJsonObject repo = data.value(QStringLiteral("n%1").arg(index)).toObject();
    if (repo.isEmpty()) continue;
    GithubNotification& n = notifications[index];
    if (n.type == QLatin1String("Issue")) {
      const QJsonObject issue = repo.value(QStringLiteral("issue")).toObject();
      if (issue.isEmpty()) continue;
      const QJsonValue state = issue.value(QStringLiteral("state"));
      if (state.isString()) {
        n.issueState = state.toString().toLower();
      } else {
        n.issueState.reset();
      }
      if (includeCommentCount) {
        if (const auto count = totalCount(issue, "comments")) n.commentCount = *count;
      }
    } else if (n.type == QLatin1String("PullRequest")) {
      const QJsonObject pr = repo.value(QStringLiteral("pullRequest")).toObject();
      if (pr.isEmpty()) continue;
      const QJsonValue state = pr.value(QStringLiteral("state"));
      if (state.isString()) {
        n.pullRequestState = state.toString().toLower();
      } else {
        n.pullRequestState.reset();
      }
      const QJsonValue merged = pr.value(QStringLiteral("mergedAt"));
      n.pullRequestIsMerged = merged.isString() ? !merged.toString().isEmpty() : merged.toBool();
      const QJsonValue draft = pr.value(QStringLiteral("isDraft"));
      if (draft.isBool()) {
        n.pullRequestIsDraft = draft.toBool();
      } else {
        n.pullRequestIsDraft.reset();
      }
      if (includeCommentCount) {
        n.commentCount =
            totalCount(pr, "comments").value_or(0) + totalCount(pr, "reviewThreads").value_or(0);
      }
    } else if (n.type == QLatin1String("Discussion")) {
      const QJsonObject discussion = repo.value(QStringLiteral("discussion")).toObject();
      if (discussion.isEmpty()) continue;
      const QJsonValue answered = discussion.value(QStringLiteral("isAnswered"));
      if (answered.isBool()) {
        n.discussionIsAnswered = answered.toBool();
      } else {
        n.discussionIsAnswered.reset();
      }
      if (includeCommentCount) {
        if (const auto count = totalCount(discussion, "comments")) n.commentCount = *count;
      }
    }
  }
  return true;
}

GithubFetchResult fetchGithubNotifications(HttpTransport& transport,
                                           const GithubFetchOptions& options) {
  GithubFetchResult result;
  const QList<QPair<QByteArray, QByteArray>> headers = {
      {QByteArrayLiteral("Authorization"), authHeader(options.token)},
      {QByteArrayLiteral("Accept"), QByteArrayLiteral("application/vnd.github.v3+json")}};
  std::vector<GithubNotification> all;
  QString next = githubNotificationsUrl(options.onlyUnread, options.maxNotification);
  while (!next.isEmpty() && static_cast<int>(all.size()) < options.maxNotification) {
    const HttpResponse response = transport.send({QByteArrayLiteral("GET"), next, headers, {}});
    if (response.status == 0) {
      result.error = QStringLiteral("no internet connection. Unable to fetch notifications.");
      return result;
    }
    if (!isSuccess(response.status)) {
      result.error = QStringLiteral("HTTP error occurred: ") + httpFailure(response);
      return result;
    }
    auto page = parseGithubNotifications(response.body);
    if (!page) {
      result.error = QStringLiteral("an unexpected error occurred: malformed notifications page");
      return result;
    }
    all.insert(all.end(), page->begin(), page->end());
    next.clear();
    if (!response.link.isEmpty()) {
      const auto links = parseGithubLinkHeader(QString::fromUtf8(response.link));
      if (const auto it = links.find(QStringLiteral("next")); it != links.end()) next = it->second;
    }
  }
  if (static_cast<int>(all.size()) > options.maxNotification) {
    all.resize(static_cast<size_t>(std::max(0, options.maxNotification)));
  }

  std::set<QString> filters;
  for (const QString& reason : options.reasonFilters) {
    if (!reason.isEmpty()) filters.insert(reason.toLower());
  }
  if (!filters.empty()) {
    std::erase_if(
        all, [&](const GithubNotification& n) { return !filters.contains(n.reason.toLower()); });
  }

  if (!options.token.isEmpty()) {
    const QString query = githubEnrichmentQuery(all, options.showCommentCount);
    if (!query.isEmpty()) {
      const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("query"), query}})
                                     .toJson(QJsonDocument::Compact);
      const HttpResponse response = transport.send(
          {QByteArrayLiteral("POST"),
           kApi + QStringLiteral("/graphql"),
           {{QByteArrayLiteral("Authorization"),
             QByteArrayLiteral("bearer ") + options.token.toUtf8()},
            {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")}},
           payload});
      if (response.status == 0) {
        qWarning().noquote() << "GitHubDataManager no internet connection. Unable to enrich "
                                "notifications via GraphQL.";
      } else if (!isSuccess(response.status)) {
        qWarning().noquote() << "GitHubDataManager GraphQL HTTP error:" << httpFailure(response);
      } else if (!applyGithubEnrichment(response.body, all, options.showCommentCount)) {
        qWarning().noquote() << "GitHubDataManager GraphQL errors in response";
      }
    }
  }
  for (GithubNotification& n : all) n.subjectApiUrl.clear();
  result.ok = true;
  result.notifications = std::move(all);
  return result;
}

HttpRequest githubMarkThreadReadRequest(const QString& token, const QString& notificationId) {
  return {QByteArrayLiteral("PATCH"),
          kApi + QStringLiteral("/notifications/threads/") + notificationId,
          {{QByteArrayLiteral("Authorization"), authHeader(token)},
           {QByteArrayLiteral("Accept"), QByteArrayLiteral("application/vnd.github.v3+json")}},
          {}};
}

HttpRequest githubMarkAllReadRequest(const QString& token, const QDateTime& nowUtc) {
  const QString lastReadAt = nowUtc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
  return {QByteArrayLiteral("PUT"),
          kApi + QStringLiteral("/notifications"),
          {{QByteArrayLiteral("Authorization"), authHeader(token)},
           {QByteArrayLiteral("Accept"), QByteArrayLiteral("application/vnd.github.v3+json")},
           {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")}},
          QByteArrayLiteral("{\"last_read_at\": \"") + lastReadAt.toUtf8() + "\"}"};
}

QString githubTokenFilePath() {
  return QDir(wabar::kernel::appDataDir()).filePath(QStringLiteral("github_token"));
}

QString resolveGithubToken(const QString& configToken, const QString& envToken,
                           const QString& tokenFilePath) {
  QString token = configToken == QLatin1String("env") ? envToken : configToken;
  if (token.isEmpty()) token = readGithubTokenFile(tokenFilePath);
  return token;
}

QString readGithubTokenFile(const QString& path) {
  QFile file(path);
  if (!file.exists()) return {};
  if (!file.open(QIODevice::ReadOnly)) {
    qCritical().noquote() << "GitHubAuth: Failed to read saved token:" << file.errorString();
    return {};
  }
  return QString::fromUtf8(file.readAll()).trimmed();
}

bool saveGithubTokenFile(const QString& path, const QString& token) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(token.toUtf8()) < 0 || !file.commit()) {
    qCritical().noquote() << "GitHubAuth failed to save token:" << file.errorString();
    return false;
  }
  qInfo().noquote() << "GitHubAuth token saved successfully.";
  return true;
}

HttpRequest githubDeviceCodeRequest() {
  return {
      QByteArrayLiteral("POST"),
      QStringLiteral("https://github.com/login/device/code"),
      {{QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")},
       {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded")}},
      formEncode(
          {{QByteArrayLiteral("client_id"), kClientId}, {QByteArrayLiteral("scope"), kScope}})};
}

GithubDeviceCode parseGithubDeviceCode(const HttpResponse& response) {
  GithubDeviceCode code;
  if (response.status == 0) {
    code.error = kNetworkError;
    return code;
  }
  if (!isSuccess(response.status)) {
    code.error = QStringLiteral("GitHub returned HTTP %1.\nDetails: %2")
                     .arg(response.status)
                     .arg(QString::fromUtf8(response.body));
    return code;
  }
  const QJsonObject data = QJsonDocument::fromJson(response.body).object();
  if (data.contains(QStringLiteral("error"))) {
    const QJsonValue description = data.value(QStringLiteral("error_description"));
    code.error = description.isUndefined() ? data.value(QStringLiteral("error")).toString()
                                           : description.toString();
    return code;
  }
  code.deviceCode = data.value(QStringLiteral("device_code")).toString();
  code.userCode = data.value(QStringLiteral("user_code")).toString();
  code.verificationUri = data.value(QStringLiteral("verification_uri")).toString();
  code.interval = data.value(QStringLiteral("interval")).toInt(5);
  code.expiresIn = data.value(QStringLiteral("expires_in")).toInt();
  return code;
}

HttpRequest githubAccessTokenRequest(const QString& deviceCode) {
  return {
      QByteArrayLiteral("POST"),
      QStringLiteral("https://github.com/login/oauth/access_token"),
      {{QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")},
       {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded")}},
      formEncode({{QByteArrayLiteral("client_id"), kClientId},
                  {QByteArrayLiteral("device_code"), deviceCode.toUtf8()},
                  {QByteArrayLiteral("grant_type"),
                   QByteArrayLiteral("urn:ietf:params:oauth:grant-type:device_code")}})};
}

GithubTokenPoll parseGithubAccessToken(const HttpResponse& response) {
  using Kind = GithubTokenPoll::Kind;
  if (response.status == 0 || !isSuccess(response.status)) return {Kind::Error, {}, kNetworkError};
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return {Kind::Error, {}, QStringLiteral("Unexpected error: ") + parseError.errorString()};
  }
  const QJsonObject data = doc.object();
  const QString error = data.value(QStringLiteral("error")).toString();
  if (error == QLatin1String("authorization_pending")) return {Kind::Pending, {}, {}};
  if (error == QLatin1String("slow_down")) return {Kind::SlowDown, {}, {}};
  if (error == QLatin1String("expired_token"))
    return {Kind::Error, {}, QStringLiteral("Device code expired.\nPlease try again.")};
  if (error == QLatin1String("access_denied"))
    return {Kind::Error, {}, QStringLiteral("Authorization was denied.")};
  if (!error.isEmpty()) return {Kind::Error, {}, QStringLiteral("Authorization failed: ") + error};
  const QString token = data.value(QStringLiteral("access_token")).toString();
  if (!token.isEmpty()) return {Kind::Token, token, {}};
  return {Kind::Pending, {}, {}};
}

}  // namespace wabar::github

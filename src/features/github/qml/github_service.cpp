#include "features/github/qml/github_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QVariantMap>
#include <QtDebug>
#include <algorithm>
#include <exception>

#include "features/github/github_format.h"
#include "kernel/qt/live_desktop.h"

namespace wabar::github {
namespace {

const QString kDefaultVerificationUri = QStringLiteral("https://github.com/login/device");

const GithubIcons& icons() {
  static const GithubIcons value;
  return value;
}

QVariantMap itemMap(const GithubNotification& n, const QDateTime& now) {
  const IconAndStates iconStates = iconAndStates(n, icons());
  return {{QStringLiteral("id"), n.id},
          {QStringLiteral("title"), n.title},
          {QStringLiteral("repo"), n.repository},
          {QStringLiteral("type"), n.type},
          {QStringLiteral("category"), n.type},
          {QStringLiteral("categoryTitle"), categoryTitle(n.type)},
          {QStringLiteral("url"), n.url},
          {QStringLiteral("unread"), n.unread},
          {QStringLiteral("comments"), n.commentCount.value_or(-1)},
          {QStringLiteral("time"), relativeTime(n.updatedAt, now)},
          {QStringLiteral("state"), iconStates.states.join(QLatin1Char(' '))},
          {QStringLiteral("icon"), iconStates.icon}};
}

class SystemGithubClient final : public GithubClient {
 public:
  explicit SystemGithubClient(HttpTransportFactory transport) : transport_(std::move(transport)) {}

  QString loadToken() override {
    return resolveGithubToken(QStringLiteral("env"), qEnvironmentVariable("WABAR_GITHUB_TOKEN"),
                              githubTokenFilePath());
  }
  bool saveToken(const QString& token) override {
    return saveGithubTokenFile(githubTokenFilePath(), token);
  }
  void fetch(const GithubFetchOptions& options,
             std::function<void(const GithubFetchResult&)> done) override {
    runAsync(&guard_,
             [options, done = std::move(done), transport = transport_]() -> std::function<void()> {
               GithubFetchResult result;
               try {
                 // One transport (one network manager) for every request of the fetch.
                 const auto http = transport ? transport() : createNetworkTransport();
                 result = fetchGithubNotifications(*http, options);
               } catch (const std::exception& error) {
                 result = {};
                 result.error = QStringLiteral("an unexpected error occurred: ") +
                                QString::fromUtf8(error.what());
               } catch (...) {
                 result = {};
                 result.error = QStringLiteral("an unexpected error occurred");
               }
               return [result, done] { done(result); };
             });
  }
  void send(const HttpRequest& request, std::function<void(const HttpResponse&)> done) override {
    sendHttpAsync(request, &guard_, std::move(done), transport_);
  }

 private:
  HttpTransportFactory transport_;
  // Context of every posted result, tested on the GUI thread only (see runAsync); destroyed with
  // the client, which drops late results.
  QObject guard_;
};

}  // namespace

std::unique_ptr<GithubClient> createSystemGithubClient(HttpTransportFactory transport) {
  return std::make_unique<SystemGithubClient>(std::move(transport));
}

GitHub::GitHub(QObject* parent)
    : GitHub(kernel::liveDesktop() ? createSystemGithubClient() : nullptr, parent) {}

GitHub::GitHub(std::unique_ptr<GithubClient> client, QObject* parent)
    : QObject(parent), client_(std::move(client)) {
  fetchOptions_.onlyUnread = true;
  fetchOptions_.maxNotification = 30;
  fetchOptions_.showCommentCount = false;
  pollTimer_.setInterval(interval_);
  connect(&pollTimer_, &QTimer::timeout, this, &GitHub::refresh);
  kickTimer_.setSingleShot(true);
  kickTimer_.setInterval(0);
  connect(&kickTimer_, &QTimer::timeout, this, &GitHub::refresh);
  signInTimer_.setSingleShot(true);
  connect(&signInTimer_, &QTimer::timeout, this, &GitHub::pollSignIn);
  if (client_) {
    token_ = client_->loadToken();
    pollTimer_.start();
    scheduleRefresh();
  }
}

GitHub::~GitHub() = default;

int GitHub::unreadCount() const {
  return static_cast<int>(std::count_if(notifications_.begin(), notifications_.end(),
                                        [](const GithubNotification& n) { return n.unread; }));
}

void GitHub::scheduleRefresh() {
  if (client_) kickTimer_.start();
}

void GitHub::setInterval(int ms) {
  ms = std::max(ms, 1000);
  if (ms == interval_) return;
  interval_ = ms;
  pollTimer_.setInterval(ms);
  emit optionsChanged();
}

void GitHub::setOnlyUnread(bool value) {
  if (value == fetchOptions_.onlyUnread) return;
  fetchOptions_.onlyUnread = value;
  emit optionsChanged();
  scheduleRefresh();
}

void GitHub::setMaxNotification(int value) {
  value = std::max(value, 0);
  if (value == fetchOptions_.maxNotification) return;
  fetchOptions_.maxNotification = value;
  emit optionsChanged();
  scheduleRefresh();
}

void GitHub::setShowComments(bool value) {
  if (value == fetchOptions_.showCommentCount) return;
  fetchOptions_.showCommentCount = value;
  emit optionsChanged();
  scheduleRefresh();
}

void GitHub::setReasonFilters(const QStringList& value) {
  if (value == fetchOptions_.reasonFilters) return;
  fetchOptions_.reasonFilters = value;
  emit optionsChanged();
  scheduleRefresh();
}

void GitHub::setCategoriesOrder(const QStringList& value) {
  if (value == categoriesOrder_) return;
  categoriesOrder_ = value;
  emit optionsChanged();
  rebuild();
  emit changed();
}

void GitHub::refresh() {
  if (!client_) return;
  if (token_.isEmpty()) {
    // The other build, or an earlier sign-in, may have saved a token meanwhile.
    token_ = client_->loadToken();
    if (token_.isEmpty()) return;
    emit changed();
  }
  if (inFlight_) {
    rerun_ = true;  // Python skips a poll while a fetch runs; an option change runs once more
    return;
  }
  inFlight_ = true;
  GithubFetchOptions options = fetchOptions_;
  options.token = token_;
  emit changed();
  client_->fetch(options, [this](const GithubFetchResult& result) { onFetched(result); });
}

void GitHub::onFetched(const GithubFetchResult& result) {
  inFlight_ = false;
  if (result.ok) {
    error_.clear();
    notifications_ = result.notifications;
  } else {
    // Python returns [] on any error, so the list is cleared.
    error_ = result.error.isEmpty() ? QStringLiteral("GitHub request failed") : result.error;
    qWarning().noquote() << "GitHub:" << error_;
    notifications_.clear();
  }
  rebuild();
  emit changed();
  if (rerun_) {
    rerun_ = false;
    refresh();
  }
}

void GitHub::rebuild() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  items_.clear();
  groups_.clear();
  for (const auto& n : notifications_) items_.append(itemMap(n, now));
  std::vector<std::string> order;
  for (const QString& category : categoriesOrder_) order.push_back(category.toStdString());
  for (const QString& category : orderedCategories(notifications_, order)) {
    QVariantList members;
    for (const auto& n : notifications_) {
      if (n.type == category) members.append(itemMap(n, now));
    }
    groups_.append(QVariantMap{{QStringLiteral("type"), category},
                               {QStringLiteral("title"), categoryTitle(category)},
                               {QStringLiteral("items"), members}});
  }
}

QString GitHub::open(const QString& id) {
  for (const auto& n : notifications_) {
    if (n.id == id) {
      const QString url = n.url;
      markRead(id);
      return url;
    }
  }
  return {};
}

void GitHub::markRead(const QString& id) {
  for (auto& n : notifications_) {
    if (n.id != id) continue;
    if (!n.unread) return;
    n.unread = false;
    if (client_ && !token_.isEmpty()) sendMark(githubMarkThreadReadRequest(token_, id));
    rebuild();
    emit changed();
    return;
  }
}

void GitHub::markAllRead() {
  if (notifications_.empty()) return;
  for (auto& n : notifications_) n.unread = false;
  if (client_ && !token_.isEmpty()) {
    sendMark(githubMarkAllReadRequest(token_, QDateTime::currentDateTimeUtc()));
  }
  rebuild();
  emit changed();
}

void GitHub::sendMark(const HttpRequest& request) {
  client_->send(request, [](const HttpResponse& response) {
    if (response.status == 0 || response.status >= 300) {
      qWarning().noquote() << "GitHub: marking as read failed:" << response.status
                           << response.reason << response.error;
    }
  });
}

QString GitHub::truncate(const QString& text, int maxSize) const {
  return truncateField(text, maxSize);
}

void GitHub::signIn() {
  if (!client_ || signingIn_) return;
  signingIn_ = true;
  signInError_.clear();
  userCode_.clear();
  verificationUri_.clear();
  const int generation = ++signInGeneration_;
  emit changed();
  client_->send(githubDeviceCodeRequest(), [this, generation](const HttpResponse& response) {
    onDeviceCode(response, generation);
  });
}

void GitHub::cancelSignIn() {
  if (!signingIn_) return;
  ++signInGeneration_;
  endSignIn({});
}

void GitHub::onDeviceCode(const HttpResponse& response, int generation) {
  if (generation != signInGeneration_ || !signingIn_) return;
  const GithubDeviceCode code = parseGithubDeviceCode(response);
  if (!code.error.isEmpty()) {
    endSignIn(code.error);
    return;
  }
  if (code.deviceCode.isEmpty() || code.userCode.isEmpty()) {
    endSignIn(QStringLiteral("GitHub did not return a device code."));
    return;
  }
  deviceCode_ = code.deviceCode;
  userCode_ = code.userCode;
  verificationUri_ =
      code.verificationUri.isEmpty() ? kDefaultVerificationUri : code.verificationUri;
  signInSeconds_ = std::max(code.interval, 5);  // Python sleeps max(interval, 5) before each poll
  signInTimer_.start(signInSeconds_ * 1000);
  emit changed();
}

void GitHub::pollSignIn() {
  if (!client_ || !signingIn_ || deviceCode_.isEmpty()) return;
  const int generation = signInGeneration_;
  client_->send(
      githubAccessTokenRequest(deviceCode_),
      [this, generation](const HttpResponse& response) { onTokenPoll(response, generation); });
}

void GitHub::onTokenPoll(const HttpResponse& response, int generation) {
  if (generation != signInGeneration_ || !signingIn_) return;
  const GithubTokenPoll poll = parseGithubAccessToken(response);
  using Kind = GithubTokenPoll::Kind;
  switch (poll.kind) {
    case Kind::Pending:
      signInTimer_.start(signInSeconds_ * 1000);
      break;
    case Kind::SlowDown:
      signInSeconds_ += 5;
      signInTimer_.start(signInSeconds_ * 1000);
      break;
    case Kind::Token: {
      token_ = poll.token;
      error_.clear();
      const bool saved = client_->saveToken(poll.token);
      endSignIn(saved ? QString() : QStringLiteral("Signed in, but the token could not be saved."));
      refresh();
      break;
    }
    case Kind::Error:
      endSignIn(poll.message);
      break;
  }
}

void GitHub::endSignIn(const QString& error) {
  signingIn_ = false;
  signInTimer_.stop();
  deviceCode_.clear();
  userCode_.clear();
  verificationUri_.clear();
  signInError_ = error;
  emit changed();
}

}  // namespace wabar::github

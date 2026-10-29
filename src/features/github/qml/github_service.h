#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>
#include <vector>

#include "features/github/github_api.h"

namespace wabar::github {

// The network and token side of the GitHub service, so tests run without GitHub. Callbacks are
// delivered on the GUI thread (a fake may call them at once).
class GithubClient {
 public:
  virtual ~GithubClient() = default;
  // The saved token: WABAR_GITHUB_TOKEN, else the token file; empty when there is none.
  virtual QString loadToken() = 0;
  virtual bool saveToken(const QString& token) = 0;
  // `options.token` is set. A failed fetch has `ok == false` and an error text.
  virtual void fetch(const GithubFetchOptions& options,
                     std::function<void(const GithubFetchResult&)> done) = 0;
  // One HTTP request (mark as read, device flow).
  virtual void send(const HttpRequest& request, std::function<void(const HttpResponse&)> done) = 0;
};

// The real client: fetches on one-shot `QThreadPool` threads (each owns its own network
// transport and ends with the fetch; nothing is joined), requests go through
// sendHttpAsync. Results are posted to the GUI thread through a guard object owned by
// the client, so a response that arrives after the client is destroyed is dropped. The token is
// handed to the fetch and never logged.
std::unique_ptr<GithubClient> createSystemGithubClient();

// GitHub notifications as a QML singleton, shared by every GitHub widget. Lives on the GUI thread
// and polls every `interval` ms through a GithubClient. `available` is false (and no client is
// created) on the offscreen platform.
//
// The token is read from the environment or the token file the Python build uses; without one no
// request is sent and `signedIn` is false. `signIn()` runs the OAuth device flow: `userCode` and
// `verificationUri` are set for the popup to show, and the token is saved when GitHub approves.
//
// `items` is a list of maps: id, title, repo, type (the GitHub subject type, e.g. PullRequest),
// category (same as type), categoryTitle ("Pull Requests"), url (the page), unread, comments
// (-1 when unknown), time (relative text), state ("closed", "merged", ...) and icon. `groups` is
// the same items by category in `categoriesOrder`: maps with type, title and items. A failed fetch
// sets `error` and clears the list, as Python does.
class GitHub : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed FINAL)
  Q_PROPERTY(int unreadCount READ unreadCount NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList items READ items NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList groups READ groups NOTIFY changed FINAL)
  Q_PROPERTY(QString error READ error NOTIFY changed FINAL)
  Q_PROPERTY(bool loading READ loading NOTIFY changed FINAL)
  Q_PROPERTY(bool signingIn READ signingIn NOTIFY changed FINAL)
  Q_PROPERTY(QString userCode READ userCode NOTIFY changed FINAL)
  Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY changed FINAL)
  Q_PROPERTY(QString signInError READ signInError NOTIFY changed FINAL)
  Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY optionsChanged FINAL)
  Q_PROPERTY(bool onlyUnread READ onlyUnread WRITE setOnlyUnread NOTIFY optionsChanged FINAL)
  Q_PROPERTY(
      int maxNotification READ maxNotification WRITE setMaxNotification NOTIFY optionsChanged FINAL)
  Q_PROPERTY(bool showComments READ showComments WRITE setShowComments NOTIFY optionsChanged FINAL)
  Q_PROPERTY(QStringList reasonFilters READ reasonFilters WRITE setReasonFilters NOTIFY
                 optionsChanged FINAL)
  Q_PROPERTY(QStringList categoriesOrder READ categoriesOrder WRITE setCategoriesOrder NOTIFY
                 optionsChanged FINAL)

 public:
  // Real client, unless unavailable (see above).
  explicit GitHub(QObject* parent = nullptr);
  // Test seam: any client (null = unavailable).
  GitHub(std::unique_ptr<GithubClient> client, QObject* parent);
  ~GitHub() override;

  bool available() const { return client_ != nullptr; }
  bool signedIn() const { return !token_.isEmpty(); }
  int unreadCount() const;
  QVariantList items() const { return items_; }
  QVariantList groups() const { return groups_; }
  QString error() const { return error_; }
  bool loading() const { return inFlight_; }
  bool signingIn() const { return signingIn_; }
  QString userCode() const { return userCode_; }
  QString verificationUri() const { return verificationUri_; }
  QString signInError() const { return signInError_; }

  int interval() const { return interval_; }
  void setInterval(int ms);
  bool onlyUnread() const { return fetchOptions_.onlyUnread; }
  void setOnlyUnread(bool value);
  int maxNotification() const { return fetchOptions_.maxNotification; }
  void setMaxNotification(int value);
  bool showComments() const { return fetchOptions_.showCommentCount; }
  void setShowComments(bool value);
  QStringList reasonFilters() const { return fetchOptions_.reasonFilters; }
  void setReasonFilters(const QStringList& value);
  QStringList categoriesOrder() const { return categoriesOrder_; }
  void setCategoriesOrder(const QStringList& value);

  // Fetches now. A fetch in progress is not duplicated; it runs once more when it ends.
  Q_INVOKABLE void refresh();
  // Marks the item read (here and on GitHub) and returns its page URL; empty for an unknown id.
  Q_INVOKABLE QString open(const QString& id);
  Q_INVOKABLE void markRead(const QString& id);
  Q_INVOKABLE void markAllRead();
  Q_INVOKABLE void signIn();
  Q_INVOKABLE void cancelSignIn();
  // Python's `text[: n - 3] + "..."` when text is longer than n code points.
  Q_INVOKABLE QString truncate(const QString& text, int maxSize) const;

  // One device-flow token request; the timer calls it. Public so tests need not wait.
  void pollSignIn();

 signals:
  void changed();
  void optionsChanged();

 private:
  void scheduleRefresh();
  void onFetched(const GithubFetchResult& result);
  void rebuild();
  void onDeviceCode(const HttpResponse& response, int generation);
  void onTokenPoll(const HttpResponse& response, int generation);
  void endSignIn(const QString& error);
  void sendMark(const HttpRequest& request);

  std::unique_ptr<GithubClient> client_;
  QString token_;
  int interval_ = 600000;
  GithubFetchOptions fetchOptions_;
  QStringList categoriesOrder_;
  QTimer pollTimer_;
  QTimer kickTimer_;
  bool inFlight_ = false;
  bool rerun_ = false;
  std::vector<GithubNotification> notifications_;
  QVariantList items_;
  QVariantList groups_;
  QString error_;

  bool signingIn_ = false;
  int signInGeneration_ = 0;
  QString deviceCode_;
  QString userCode_;
  QString verificationUri_;
  QString signInError_;
  int signInSeconds_ = 5;
  QTimer signInTimer_;
};

}  // namespace wabar::github

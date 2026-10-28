#pragma once

#include <QMutex>
#include <QObject>
#include <memory>
#include <vector>

#include "platform/github_api.h"

namespace yasb::widgets {

// Token shared with the polling thread; guarded because login sets it on the GUI thread.
class GithubTokenHolder {
 public:
  QString get() const {
    QMutexLocker lock(&mutex_);
    return token_;
  }
  void set(const QString& token) {
    QMutexLocker lock(&mutex_);
    token_ = token;
  }

 private:
  mutable QMutex mutex_;
  QString token_;
};

// GUI-thread notification list shared by every GithubWidget polling with the same settings (the
// role of Python's GitHubDataManager). It is a child of the shared PollingWorker, so it lives as
// long as the runtime's polling-worker registry.
class GithubDataStore final : public QObject {
  Q_OBJECT
 public:
  explicit GithubDataStore(QObject* parent = nullptr) : QObject(parent) {}

  // Finds the store parented to `owner`, creating it on first use (`created` tells which).
  static GithubDataStore* forOwner(QObject* owner, bool* created = nullptr);

  const std::vector<platform::GithubNotification>& notifications() const { return data_; }
  bool hasData() const { return hasData_; }
  const std::shared_ptr<GithubTokenHolder>& tokenHolder() const { return tokenHolder_; }
  void adoptTokenHolder(std::shared_ptr<GithubTokenHolder> holder) {
    tokenHolder_ = std::move(holder);
  }
  int unreadCount() const;

  // A failed fetch is logged and clears the list (Python returns [] on any error); a skipped
  // poll (empty error) changes nothing.
  void applyFetch(const platform::GithubFetchResult& result);
  void setNotifications(std::vector<platform::GithubNotification> notifications);
  void markRead(const QString& notificationId);
  void markAllRead();

 signals:
  void changed();

 private:
  std::vector<platform::GithubNotification> data_;
  bool hasData_ = false;
  std::shared_ptr<GithubTokenHolder> tokenHolder_ = std::make_shared<GithubTokenHolder>();
};

}  // namespace yasb::widgets

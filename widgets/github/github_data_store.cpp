#include "widgets/github/github_data_store.h"

#include <QtDebug>
#include <algorithm>

namespace yasb::widgets {

GithubDataStore* GithubDataStore::forOwner(QObject* owner, bool* created) {
  if (created) *created = false;
  if (!owner) return nullptr;
  if (auto* store = owner->findChild<GithubDataStore*>(QString(), Qt::FindDirectChildrenOnly))
    return store;
  if (created) *created = true;
  return new GithubDataStore(owner);
}

int GithubDataStore::unreadCount() const {
  return static_cast<int>(
      std::count_if(data_.begin(), data_.end(), [](const auto& n) { return n.unread; }));
}

void GithubDataStore::applyFetch(const platform::GithubFetchResult& result) {
  // An empty error means the poll was skipped (no token): nothing to apply.
  if (!result.ok && result.error.isEmpty()) return;
  if (!result.ok) {
    // Python's _get_github_notifications returns [] on any error, so the list is cleared.
    qCritical().noquote() << "GitHubDataManager" << result.error;
    setNotifications({});
    return;
  }
  setNotifications(result.notifications);
}

void GithubDataStore::setNotifications(std::vector<platform::GithubNotification> notifications) {
  data_ = std::move(notifications);
  hasData_ = true;
  emit changed();
}

void GithubDataStore::markRead(const QString& notificationId) {
  for (auto& n : data_) {
    if (n.id == notificationId) {
      n.unread = false;
      break;
    }
  }
  emit changed();
}

void GithubDataStore::markAllRead() {
  for (auto& n : data_) n.unread = false;
  emit changed();
}

}  // namespace yasb::widgets

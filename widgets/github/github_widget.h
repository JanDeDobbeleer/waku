#pragma once

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QUrl>
#include <functional>
#include <optional>
#include <vector>

#include "core/config/options/github.h"
#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "core/widgets/stat_popup.h"
#include "widgets/github/github_data_store.h"

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

class GithubAuthDialog;

// QLabel with a dot painted on one corner of the icon (github.py NotificationLabel).
class NotificationLabel final : public QLabel {
  Q_OBJECT
 public:
  NotificationLabel(const QString& text, const QString& color, const QString& corner,
                    const std::vector<int>& margin, QWidget* parent = nullptr);
  void showDot(bool enabled);
  bool dotVisible() const { return showDot_; }
  // Top-left of the dot's 6px box in label coordinates.
  QPoint dotOrigin() const;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  bool showDot_ = false;
  QColor color_;
  QString corner_;
  int marginX_ = 1;
  int marginY_ = 1;
};

// Seams for tests; defaults talk to GitHub and the desktop.
struct GithubWidgetServices {
  // Overrides token resolution (option / YASB_GITHUB_TOKEN / token file).
  std::optional<QString> token;
  bool startPolling = true;
  // Shared store to use instead of the polling worker's.
  GithubDataStore* store = nullptr;
  std::function<void(const QString& token, const QString& notificationId)> markThreadRead;
  std::function<void(const QString& token)> markAllRead;
  std::function<void(const QUrl& url)> openUrl;
  std::function<QDateTime()> nowUtc;
  // Starts the device-flow login; it reports a token through GithubWidget::setToken.
  std::function<void()> startLogin;
};

// Port of yasb:src/core/widgets/yasb/github.py. Fetching runs on a shared PollingWorker thread
// (see GithubDataStore); mark-as-read requests run on QThreadPool threads (platform/github_api).
class GithubWidget final : public BaseWidget {
  Q_OBJECT
 public:
  explicit GithubWidget(const WidgetContext& context, GithubWidgetServices services = {});

  const std::vector<QLabel*>& primaryLabels() const { return widgets_; }
  const std::vector<QLabel*>& altLabels() const { return widgetsAlt_; }
  NotificationLabel* notificationLabel(bool alt) const {
    return alt ? notificationLabelAlt_ : notificationLabel_;
  }
  bool showingAlt() const { return showAltLabel_; }
  GithubDataStore* store() const { return store_; }
  PinnablePopup* menu() const { return menu_; }
  bool hasToken() const { return !token_.isEmpty(); }

  // Called after a successful device-flow login.
  void setToken(const QString& token);
  void updateLabel();
  void showMenu();

 private:
  void createLabels();
  void toggleLabel();
  void toggleMenu();
  void startPolling();
  void attachStore(GithubDataStore* store);
  QFrame* createNotificationItem(const platform::GithubNotification& notification,
                                 const QStringList& extraClasses, QWidget* parent);
  void markAsRead(const QString& notificationId, QFrame* container);
  void markAllAsRead();
  void startLogin();

  config::options::GithubConfig options_;
  bar::GlobalState& globalState_;
  PollingWorkerRegistry& pollingWorkers_;
  GithubWidgetServices services_;
  QString token_;
  bool showAltLabel_ = false;
  std::vector<QLabel*> widgets_;
  std::vector<QLabel*> widgetsAlt_;
  NotificationLabel* notificationLabel_ = nullptr;
  NotificationLabel* notificationLabelAlt_ = nullptr;
  QPointer<GithubDataStore> store_;
  QPointer<PinnablePopup> menu_;
  QPointer<GithubAuthDialog> authDialog_;
};

}  // namespace yasb::widgets

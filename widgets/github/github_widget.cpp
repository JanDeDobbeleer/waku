#include "widgets/github/github_widget.h"

#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMutex>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtDebug>
#include <atomic>
#include <exception>
#include <map>
#include <memory>

#include "core/bar/theme_class.h"
#include "core/widgets/label_format.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/tooltip.h"
#include "widgets/github/github_auth_dialog.h"
#include "widgets/github/github_format.h"

namespace yasb::widgets {

namespace {

platform::GithubFetchResult fetchOnce(const platform::GithubFetchOptions& options) {
  // An empty result with no error means "skipped" (no token yet).
  if (options.token.isEmpty()) return {};
  try {
    return platform::fetchGithubNotifications(*platform::createNetworkTransport(), options);
  } catch (const std::exception& error) {
    platform::GithubFetchResult failed;
    failed.error =
        QStringLiteral("an unexpected error occurred: ") + QString::fromUtf8(error.what());
    return failed;
  }
}

// Frame that reports mouse presses (Python assigns mousePressEvent on the item container).
class PressFrame final : public QFrame {
 public:
  explicit PressFrame(QWidget* parent) : QFrame(parent) {}
  std::function<void()> onPress;

 protected:
  void mousePressEvent(QMouseEvent*) override {
    if (onPress) onPress();
  }
};

class PressLabel final : public QLabel {
 public:
  PressLabel(const QString& text, std::function<void()> press)
      : QLabel(text), onPress_(std::move(press)) {}

 protected:
  void mousePressEvent(QMouseEvent*) override {
    if (onPress_) onPress_();
  }

 private:
  std::function<void()> onPress_;
};

// QAbstractScrollArea::setViewportMargins is protected.
class MenuScrollArea final : public QScrollArea {
 public:
  using QAbstractScrollArea::setViewportMargins;
};

QString toQ(const std::string& value) { return QString::fromStdString(value); }

const QRegularExpression& spanSplit() {
  static const QRegularExpression re(QStringLiteral("(<span.*?>.*?</span>)"));
  return re;
}

// re.split with a capture group: text, span, text, span, ..., text (empty pieces kept).
QStringList splitKeepingSpans(const QString& content) {
  QStringList pieces;
  qsizetype last = 0;
  auto it = spanSplit().globalMatch(content);
  while (it.hasNext()) {
    const auto m = it.next();
    pieces << content.mid(last, m.capturedStart() - last) << m.captured(1);
    last = m.capturedEnd();
  }
  pieces << content.mid(last);
  return pieces;
}

QString stripSpan(const QString& part) {
  static const QRegularExpression re(QStringLiteral("<span.*?>|</span>"));
  QString text = part;
  return text.remove(re).trimmed();
}

QStringList splitClasses(const QString& value) {
  static const QRegularExpression ws(QStringLiteral("\\s+"));
  return value.split(ws, Qt::SkipEmptyParts);
}

github::GithubIcons toGithubIcons(const config::options::GithubIconsConfig& icons) {
  github::GithubIcons result;
  result.issue = icons.issue;
  result.issue_closed = icons.issue_closed;
  result.pull_request = icons.pull_request;
  result.pull_request_closed = icons.pull_request_closed;
  result.pull_request_merged = icons.pull_request_merged;
  result.pull_request_draft = icons.pull_request_draft;
  result.release = icons.release;
  result.discussion = icons.discussion;
  result.discussion_answered = icons.discussion_answered;
  result.checksuite = icons.checksuite;
  result.default_ = icons.default_;
  return result;
}

platform::GithubFetchOptions fetchOptions(const config::options::GithubConfig& options) {
  platform::GithubFetchOptions fetch;
  fetch.onlyUnread = options.only_unread;
  fetch.maxNotification = options.max_notification;
  for (const std::string& reason : options.reason_filters) fetch.reasonFilters << toQ(reason);
  fetch.showCommentCount = options.show_comment_count;
  return fetch;
}

}  // namespace

NotificationLabel::NotificationLabel(const QString& text, const QString& color,
                                     const QString& corner, const std::vector<int>& margin,
                                     QWidget* parent)
    : QLabel(text, parent),
      color_(color),
      corner_(corner),
      marginX_(margin.size() > 0 ? margin[0] : 1),
      marginY_(margin.size() > 1 ? margin[1] : 1) {}

void NotificationLabel::showDot(bool enabled) {
  showDot_ = enabled;
  update();
}

QPoint NotificationLabel::dotOrigin() const {
  constexpr int radius = 6;
  if (corner_ == QLatin1String("top_left")) return {marginX_, marginY_};
  if (corner_ == QLatin1String("top_right")) return {width() - radius - marginX_, marginY_};
  if (corner_ == QLatin1String("bottom_left")) return {marginX_, height() - radius - marginY_};
  if (corner_ == QLatin1String("bottom_right"))
    return {width() - radius - marginX_, height() - radius - marginY_};
  return {0, 0};
}

void NotificationLabel::paintEvent(QPaintEvent* event) {
  QLabel::paintEvent(event);
  if (!showDot_) return;
  constexpr int radius = 6;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(color_);
  painter.setPen(Qt::NoPen);
  const QPoint origin = dotOrigin();
  painter.drawEllipse(QPoint(origin.x() + radius / 2, origin.y() + radius / 2), radius / 2,
                      radius / 2);
}

GithubWidget::GithubWidget(const WidgetContext& context, GithubWidgetServices services)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("github-widget "), 0),
      options_(std::get<config::options::GithubConfig>(context.typedOptions)),
      globalState_(context.globalState),
      pollingWorkers_(context.pollingWorkers),
      services_(std::move(services)) {
  if (services_.token) {
    token_ = *services_.token;
  } else {
    token_ =
        platform::resolveGithubToken(toQ(options_.token), qEnvironmentVariable("YASB_GITHUB_TOKEN"),
                                     platform::githubTokenFilePath());
  }

  createLabels();
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"), [this](const QStringList&) { toggleMenu(); });
  CallbackBinding binding;
  binding.left = QStringLiteral("toggle_menu");
  binding.right = QStringLiteral("toggle_label");
  setCallbackBinding(binding);

  if (options_.hide_empty) setVisible(false);

  if (services_.store) {
    attachStore(services_.store);
  } else if (services_.startPolling) {
    startPolling();
  }
}

void GithubWidget::createLabels() {
  QLayout* layout = labelContainer()->layout();
  auto build = [&](const QString& content, bool isAlt, std::vector<QLabel*>& out) {
    for (QString piece : splitKeepingSpans(content)) {
      if (piece.isEmpty()) continue;
      piece = piece.trimmed();
      if (piece.isEmpty()) continue;
      QLabel* label = nullptr;
      if (isSpanPart(piece)) {
        static const QRegularExpression classRe(QStringLiteral("class=([\"'])([^\"']+?)\\1"));
        const auto classMatch = classRe.match(piece);
        const QString cssClass =
            classMatch.hasMatch() ? classMatch.captured(2) : QStringLiteral("icon");
        auto* notification =
            new NotificationLabel(stripSpan(piece), toQ(options_.notification_dot.color),
                                  toQ(options_.notification_dot.corner),
                                  options_.notification_dot.margin, labelContainer());
        notification->setProperty("class", cssClass);
        (isAlt ? notificationLabelAlt_ : notificationLabel_) = notification;
        label = notification;
      } else {
        label = new QLabel(piece, labelContainer());
        label->setProperty("class", QStringLiteral("label"));
      }
      label->setAlignment(Qt::AlignCenter);
      if (token_.isEmpty() && options_.tooltip) {
        setTooltip(label, QStringLiteral("Error: Token not configured"), globalState_);
      }
      layout->addWidget(label);
      out.push_back(label);
      if (isAlt) {
        label->hide();
      } else {
        label->show();
      }
    }
  };
  build(toQ(options_.label), false, widgets_);
  build(toQ(options_.label_alt), true, widgetsAlt_);
}

void GithubWidget::toggleLabel() {
  showAltLabel_ = !showAltLabel_;
  for (QLabel* label : widgets_) label->setVisible(!showAltLabel_);
  for (QLabel* label : widgetsAlt_) label->setVisible(showAltLabel_);
  updateLabel();
}

void GithubWidget::toggleMenu() {
  // Another bar's widget may have completed the login.
  if (token_.isEmpty() && store_) token_ = store_->tokenHolder()->get();
  if (token_.isEmpty()) {
    startLogin();
    return;
  }
  showMenu();
}

void GithubWidget::startPolling() {
  // The store (shared by all bars' widgets) owns the token the polling thread reads.
  auto holder = std::make_shared<GithubTokenHolder>();
  holder->set(token_);
  const platform::GithubFetchOptions fetch = fetchOptions(options_);

  PollingWorker* worker =
      pollingWorkers_.get("github", options_.update_interval * 1000, [holder, fetch] {
        auto running = std::make_shared<std::atomic<bool>>(false);
        return [holder, fetch, running]() {
          // Python skips a cycle while a fetch is in progress.
          if (running->exchange(true)) return QVariant::fromValue(platform::GithubFetchResult{});
          platform::GithubFetchOptions options = fetch;
          options.token = holder->get();
          const platform::GithubFetchResult result = fetchOnce(options);
          running->store(false);
          return QVariant::fromValue(result);
        };
      });
  if (!worker) return;
  bool created = false;
  GithubDataStore* store = GithubDataStore::forOwner(worker, &created);
  if (created) {
    store->adoptTokenHolder(holder);
    connect(worker, &PollingWorker::dataReady, store, [store](const QVariant& value) {
      store->applyFetch(value.value<platform::GithubFetchResult>());
    });
  }
  attachStore(store);
  if (created) worker->start();
}

void GithubWidget::attachStore(GithubDataStore* store) {
  store_ = store;
  connect(store, &GithubDataStore::changed, this, &GithubWidget::updateLabel);
  if (store->hasData()) updateLabel();
}

void GithubWidget::setToken(const QString& token) {
  token_ = token;
  if (store_) store_->tokenHolder()->set(token);
  if (!store_ || services_.store) return;
  // Immediate fetch after login (GitHubDataManager.set_token).
  platform::GithubFetchOptions fetch = fetchOptions(options_);
  fetch.token = token;
  QPointer<GithubDataStore> store = store_;
  QThreadPool::globalInstance()->start([fetch, store] {
    const platform::GithubFetchResult result = fetchOnce(fetch);
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [store, result] {
          if (store) store->applyFetch(result);
        },
        Qt::QueuedConnection);
  });
}

void GithubWidget::startLogin() {
  if (services_.startLogin) {
    services_.startLogin();
    return;
  }
  if (authDialog_) return;
  authDialog_ = new GithubAuthDialog(platform::githubTokenFilePath());
  connect(authDialog_, &GithubAuthDialog::authCompleted, this,
          [this](const QString& token) { setToken(token); });
  authDialog_->show();
}

void GithubWidget::updateLabel() {
  try {
    const int count = store_ ? store_->unreadCount() : 0;
    if (options_.hide_empty) {
      setVisible(count > 0);
      if (count == 0) return;
    }
    const std::vector<QLabel*>& active = showAltLabel_ ? widgetsAlt_ : widgets_;
    const QString content = toQ(showAltLabel_ ? options_.label_alt : options_.label);

    // Python: [part.strip() for part in re.split(...) if part]. Whitespace-only parts stay as
    // empty strings and can shift indices; mirrored here.
    QStringList parts = splitKeepingSpans(content);
    parts.removeAll(QString());
    for (QString& part : parts) part = part.trimmed();

    if (options_.notification_dot.enabled) {
      if (!showAltLabel_ && notificationLabel_) notificationLabel_->showDot(count > 0);
      if (showAltLabel_ && notificationLabelAlt_) notificationLabelAlt_->showDot(count > 0);
    }

    for (qsizetype index = 0; index < parts.size(); ++index) {
      if (static_cast<size_t>(index) >= active.size()) continue;
      QLabel* label = active[static_cast<size_t>(index)];
      const QString& part = parts[index];
      if (isSpanPart(part)) {
        label->setText(stripSpan(part));
        if (options_.tooltip) {
          setTooltip(label, QStringLiteral("Notifications %1").arg(count), globalState_);
        }
        QStringList classes = splitClasses(label->property("class").toString());
        const QString notificationClass = QStringLiteral("new-notification");
        if (count > 0) {
          classes << notificationClass;
        } else {
          classes.removeAll(notificationClass);
        }
        label->setProperty("class", classes.join(QLatin1Char(' ')));
      } else {
        try {
          label->setText(QString::fromStdString(pyFormat(part.toStdString(), {{"data", count}})));
        } catch (const FormatError& error) {
          qWarning().noquote() << "GitHub label format failed:" << error.what();
        }
      }
      bar::refreshWidgetStyle(label);
    }
  } catch (const std::exception& error) {
    qCritical().noquote() << "GitHub updateLabel failed:" << error.what();
  }
}

void GithubWidget::markAsRead(const QString& notificationId, QFrame* container) {
  if (store_) store_->markRead(notificationId);
  if (services_.markThreadRead) {
    services_.markThreadRead(token_, notificationId);
  } else if (!token_.isEmpty()) {
    platform::sendHttpAsync(platform::githubMarkThreadReadRequest(token_, notificationId), nullptr,
                            [](const platform::HttpResponse& response) {
                              if (response.status == 0 || response.status >= 300) {
                                qCritical().noquote()
                                    << "GitHubDataManager error marking notification as read:"
                                    << response.status << response.reason << response.error;
                              }
                            });
  }
  QStringList classes = splitClasses(container->property("class").toString());
  classes.removeAll(QStringLiteral("new"));
  container->setProperty("class", classes.join(QLatin1Char(' ')));
  bar::refreshWidgetStyle(container);
}

void GithubWidget::markAllAsRead() {
  if (store_) store_->markAllRead();
  if (services_.markAllRead) {
    services_.markAllRead(token_);
  } else if (!token_.isEmpty()) {
    const QDateTime now = services_.nowUtc ? services_.nowUtc() : QDateTime::currentDateTimeUtc();
    platform::sendHttpAsync(platform::githubMarkAllReadRequest(token_, now), nullptr,
                            [](const platform::HttpResponse& response) {
                              if (response.status == 0 || response.status >= 300) {
                                qCritical().noquote()
                                    << "GitHubDataManager error marking all as read:"
                                    << response.status << response.reason << response.error;
                              }
                            });
  }
  if (menu_) menu_->hide();
}

QFrame* GithubWidget::createNotificationItem(const platform::GithubNotification& n,
                                             const QStringList& extraClasses, QWidget* parent) {
  using github::truncateField;
  const int maxSize = options_.max_field_size;
  const QString title = truncateField(n.title, maxSize);

  QString description = options_.menu.show_categories
                            ? n.repository
                            : QStringLiteral("%1 • %2").arg(n.type, n.repository);
  const QDateTime now = services_.nowUtc ? services_.nowUtc() : QDateTime::currentDateTimeUtc();
  const QString relative = github::relativeTime(n.updatedAt, now);
  if (!relative.isEmpty()) {
    description = QStringLiteral("%1 • Updated %2").arg(description, relative);
  }
  description = truncateField(description, maxSize);

  const github::IconAndStates iconStates = github::iconAndStates(n, toGithubIcons(options_.icons));
  const QString baseClass = n.type.toLower();

  QStringList classes{QStringLiteral("item")};
  if (n.unread) classes << QStringLiteral("new");
  if (!baseClass.isEmpty()) classes << baseClass;
  classes << extraClasses;
  classes.removeDuplicates();

  auto* container = new PressFrame(parent);
  container->onPress = [this, id = n.id, url = n.url, container] {
    markAsRead(id, container);
    if (menu_) menu_->hide();
    if (services_.openUrl) {
      services_.openUrl(QUrl(url));
    } else {
      QDesktopServices::openUrl(QUrl(url));
    }
  };
  container->setProperty("class", classes.join(QLatin1Char(' ')));
  container->setContentsMargins(0, 0, 0, 0);

  auto* iconLabel = new QLabel(iconStates.icon);
  QStringList iconClasses{QStringLiteral("icon")};
  if (!baseClass.isEmpty()) iconClasses << baseClass;
  iconClasses << iconStates.states;
  iconClasses.removeDuplicates();
  iconLabel->setProperty("class", iconClasses.join(QLatin1Char(' ')));

  auto* titleLabel = new QLabel(title);
  titleLabel->setTextFormat(Qt::PlainText);
  titleLabel->setProperty("class", QStringLiteral("title"));
  titleLabel->setContentsMargins(0, 0, 0, 0);

  auto* descriptionLabel = new QLabel(description);
  descriptionLabel->setTextFormat(Qt::PlainText);
  descriptionLabel->setProperty("class", QStringLiteral("description"));
  descriptionLabel->setContentsMargins(0, 0, 0, 0);

  auto* textContent = new QWidget;
  auto* textLayout = new QVBoxLayout(textContent);
  textLayout->addWidget(titleLabel);
  textLayout->addWidget(descriptionLabel);
  textLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(0);

  auto* containerLayout = new QHBoxLayout(container);
  containerLayout->addWidget(iconLabel);
  containerLayout->addWidget(textContent, 1);

  if (options_.show_comment_count && n.commentCount) {
    auto* wrapper = new QWidget;
    auto* wrapperLayout = new QHBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(8, 0, 0, 0);
    wrapperLayout->setSpacing(0);
    const QString commentIcon = toQ(options_.icons.comment).trimmed();
    if (!commentIcon.isEmpty()) {
      auto* commentIconLabel = new QLabel(commentIcon);
      commentIconLabel->setProperty("class", QStringLiteral("comment-icon"));
      commentIconLabel->setAlignment(Qt::AlignCenter);
      wrapperLayout->addWidget(commentIconLabel);
    }
    auto* commentValue = new QLabel(QString::number(*n.commentCount));
    commentValue->setProperty("class", QStringLiteral("comment-count"));
    commentValue->setAlignment(Qt::AlignCenter);
    wrapperLayout->addWidget(commentValue);
    containerLayout->addWidget(wrapper);
  }
  containerLayout->setAlignment(Qt::AlignLeft);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);
  return container;
}

void GithubWidget::showMenu() {
  if (menu_) menu_->deleteLater();
  const std::vector<platform::GithubNotification> list =
      store_ ? store_->notifications() : std::vector<platform::GithubNotification>{};
  const int unread = store_ ? store_->unreadCount() : 0;

  const auto& m = options_.menu;
  auto* menu = new PinnablePopup(this, globalState_, m.blur, m.round_corners,
                                 toQ(m.round_corners_type), toQ(m.border_color));
  menu_ = menu;
  menu->setProperty("class", QStringLiteral("github-menu"));

  auto* mainLayout = new QVBoxLayout(menu);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  auto* header =
      new QLabel(QStringLiteral("<span style='font-weight:bold'>GitHub</span> Notifications"));
  header->setProperty("class", QStringLiteral("header"));
  mainLayout->addWidget(header);

  auto* scrollArea = new MenuScrollArea;
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setViewportMargins(0, 0, -4, 0);
  scrollArea->setStyleSheet(QStringLiteral(
      "QScrollArea { background: transparent; border: none; border-radius:0; }\n"
      "QScrollBar:vertical { border: none; background:transparent; width: 4px; }\n"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }\n"
      "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.2); min-height: 10px; "
      "border-radius: 2px; }\n"
      "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.35); }\n"
      "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical { height: 0px; }\n"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
  mainLayout->addWidget(scrollArea);

  auto* scrollWidget = new QWidget;
  scrollWidget->setProperty("class", QStringLiteral("contents"));
  auto* scrollLayout = new QVBoxLayout(scrollWidget);
  scrollLayout->setAlignment(Qt::AlignTop);
  scrollLayout->setContentsMargins(0, 0, 0, 0);
  scrollLayout->setSpacing(0);

  auto buildSection = [&](const std::vector<const platform::GithubNotification*>& items) {
    auto* section = new QFrame;
    section->setProperty("class", QStringLiteral("section"));
    auto* sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(0);
    for (size_t i = 0; i < items.size(); ++i) {
      QStringList position;
      if (i == 0) position << QStringLiteral("first");
      if (i == items.size() - 1) position << QStringLiteral("last");
      sectionLayout->addWidget(createNotificationItem(*items[i], position, section));
    }
    scrollLayout->addWidget(section);
  };

  if (!list.empty()) {
    if (m.show_categories) {
      std::map<QString, std::vector<const platform::GithubNotification*>> grouped;
      for (const auto& n : list) grouped[n.type].push_back(&n);
      for (const QString& category : github::orderedCategories(list, m.categories_order)) {
        auto* sectionHeader = new QLabel(github::categoryTitle(category));
        sectionHeader->setProperty("class", QStringLiteral("section-header"));
        sectionHeader->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        scrollLayout->addWidget(sectionHeader);
        buildSection(grouped[category]);
      }
    } else {
      std::vector<const platform::GithubNotification*> all;
      for (const auto& n : list) all.push_back(&n);
      buildSection(all);
    }
  } else {
    auto* logo = new QLabel(toQ(options_.icons.github_logo));
    logo->setStyleSheet(QStringLiteral("font-size:88px;font-weight:400"));
    auto* logoOpacity = new QGraphicsOpacityEffect;
    logoOpacity->setOpacity(0.4);
    logo->setGraphicsEffect(logoOpacity);

    auto* noData = new QLabel(QStringLiteral("No unread notifications"));
    noData->setStyleSheet(QStringLiteral("font-size:18px;font-weight:400;font-family: Segoe UI"));
    auto* noDataOpacity = new QGraphicsOpacityEffect;
    noDataOpacity->setOpacity(0.5);
    noData->setGraphicsEffect(noDataOpacity);

    auto* center = new QVBoxLayout;
    center->addStretch();
    center->addWidget(logo, 0, Qt::AlignCenter);
    center->addWidget(noData, 0, Qt::AlignCenter);
    center->addStretch();
    scrollLayout->addLayout(center);
  }
  scrollArea->setWidget(scrollWidget);

  if (!list.empty()) {
    auto* footer = new QFrame;
    footer->setProperty("class", QStringLiteral("footer"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);
    auto* unreadLabel = new QLabel(QStringLiteral("Unread notifications (%1)").arg(unread));
    unreadLabel->setProperty("class", QStringLiteral("label"));
    footerLayout->addWidget(unreadLabel);
    footerLayout->addStretch();
    auto* markAll = new PressLabel(QStringLiteral("Mark all as read"), [this] { markAllAsRead(); });
    markAll->setProperty("class", QStringLiteral("label"));
    footerLayout->addWidget(markAll);
    mainLayout->addWidget(footer);
  }

  menu->adjustSize();
  menu->setPosition(toQ(m.alignment), toQ(m.direction), m.offset_left, m.offset_top);
  menu->show();
}

}  // namespace yasb::widgets

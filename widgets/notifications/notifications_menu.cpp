#include "widgets/notifications/notifications_menu.h"

#include <QDebug>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QVBoxLayout>
#include <exception>
#include <map>

#include "core/bar/theme_class.h"
#include "core/widgets/tooltip.h"
#include "widgets/menu_support/elided_label.h"

namespace yasb::widgets {
namespace {

QString trimmedMonth(int month) {
  static const char* const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return QString::fromLatin1(names[(month - 1) % 12]);
}

QHBoxLayout* zeroHBox(QWidget* parent = nullptr) {
  auto* layout = new QHBoxLayout(parent);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  return layout;
}

QVBoxLayout* zeroVBox(QWidget* parent = nullptr) {
  auto* layout = new QVBoxLayout(parent);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  return layout;
}

// Groups by AUMID in order of first appearance (a Python dict keeps insertion order).
std::vector<std::vector<platform::NotificationItem>> groupByApp(
    const std::vector<platform::NotificationItem>& items) {
  std::vector<std::vector<platform::NotificationItem>> groups;
  std::map<QString, size_t> index;
  for (const auto& item : items) {
    const auto found = index.find(item.aumid);
    if (found == index.end()) {
      index[item.aumid] = groups.size();
      groups.push_back({item});
    } else {
      groups[found->second].push_back(item);
    }
  }
  return groups;
}

}  // namespace

QString shortRelativeTime(const QDateTime& created, const QDateTime& now) {
  if (!created.isValid()) return {};
  const double seconds = static_cast<double>(created.msecsTo(now)) / 1000.0;
  const double minutes = seconds / 60;
  const double hours = minutes / 60;
  const double days = hours / 24;
  if (seconds < 60) return QStringLiteral("now");
  if (minutes < 60) return QString::number(static_cast<int>(minutes)) + QLatin1Char('m');
  if (hours < 24) return QString::number(static_cast<int>(hours)) + QLatin1Char('h');
  if (days < 7) return QString::number(static_cast<int>(days)) + QLatin1Char('d');
  const QDate date = created.date();
  if (days < 365)
    return QString::number(date.day()) + QLatin1Char(' ') + trimmedMonth(date.month());
  return trimmedMonth(date.month()) + QLatin1Char(' ') + QString::number(date.year());
}

QString NotificationsMenu::pictureKey(const QString& path, const QString& shape) {
  return shape + QLatin1Char(':') + path;
}

QString NotificationsMenu::iconKey(const QString& aumid) { return QStringLiteral("icon:") + aumid; }

NotificationsMenu::NotificationsMenu(QWidget* anchor, platform::NotificationService* service,
                                     platform::DndWatcher* dndWatcher,
                                     NotificationsMenuOptions options, NotificationsMenuHooks hooks,
                                     bar::GlobalState& globalState)
    : QObject(anchor),
      anchor_(anchor),
      service_(service),
      dndWatcher_(dndWatcher),
      options_(std::move(options)),
      hooks_(std::move(hooks)),
      globalState_(globalState) {
  pool_.setMaxThreadCount(2);
  refreshTimer_.setSingleShot(true);
  refreshTimer_.setInterval(kPictureRefreshDelayMs);
  connect(&refreshTimer_, &QTimer::timeout, this, &NotificationsMenu::refreshItems);
  connect(&imageSignals_, &ImageSignals::loaded, this, &NotificationsMenu::onImageLoaded);
  if (service_)
    connect(service_, &platform::NotificationService::itemsChanged, this,
            &NotificationsMenu::onItemsChanged);
  if (dndWatcher_)
    connect(dndWatcher_, &platform::DndWatcher::statusChanged, this,
            &NotificationsMenu::updateDndButton);
}

NotificationsMenu::~NotificationsMenu() { pool_.waitForDone(); }

double NotificationsMenu::dpr() const { return anchor_ ? anchor_->devicePixelRatioF() : 1.0; }

bool NotificationsMenu::isOpen() const { return popup_ && popup_->isVisible(); }

void NotificationsMenu::toggle() {
  if (MenuPopup* open = MenuPopup::openFor(anchor_)) {
    open->hideAnimated();
    return;
  }
  show();
}

void NotificationsMenu::close() {
  if (popup_) popup_->hideAnimated();
}

void NotificationsMenu::show() {
  popup_ = new MenuPopup(anchor_, options_.blur, options_.roundCorners, options_.roundCornersType,
                         options_.borderColor, false);
  popup_->setCssClass(QStringLiteral("notification-menu"));
  expanded_.clear();

  auto* layout = zeroVBox(popup_);
  layout->addWidget(createHeader());
  scroll_ = createScrollArea();
  layout->addWidget(scroll_);
  layout->addWidget(createFooter());

  refreshItems();
  popup_->show();
  if (service_) service_->refresh();
}

QWidget* NotificationsMenu::createHeader() {
  auto* header = new QFrame;
  header->setProperty("class", QStringLiteral("header"));
  auto* layout = zeroHBox(header);

  auto* title = new QLabel(QStringLiteral("Notifications"));
  title->setProperty("class", QStringLiteral("title"));
  layout->addWidget(title, 1);

  dnd_ = nullptr;
  const QString status = hooks_.dndStatus ? hooks_.dndStatus() : QStringLiteral("unknown");
  if (status != QLatin1String("unknown")) {
    dnd_ = createButton(QString(), QStringLiteral("dnd"), [this] { toggleDnd(); });
    updateDndButton(status);
    layout->addWidget(dnd_);
  }

  layout->addWidget(createButton(QStringLiteral("Clear all"), QStringLiteral("clear-all"),
                                 [this] { clearAll(); }));
  return header;
}

QScrollArea* NotificationsMenu::createScrollArea() {
  auto* area = new QScrollArea;
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->setStyleSheet(QStringLiteral(
      "QScrollArea { background: transparent; border: none; border-radius:0; }\n"
      "QScrollBar:vertical { border: none; background:transparent; width: 4px; }\n"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }\n"
      "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.2); min-height: 10px; "
      "border-radius: 2px; }\n"
      "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.35); }\n"
      "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical { height: 0px; }\n"));
  // the viewport paints the palette background, which the stylesheet above doesn't reach
  if (QWidget* viewport = area->viewport()) viewport->setAutoFillBackground(false);
  return area;
}

QWidget* NotificationsMenu::createFooter() {
  auto* footer = new QFrame;
  footer->setProperty("class", QStringLiteral("footer"));
  auto* layout = zeroHBox(footer);
  layout->addWidget(createButton(QStringLiteral("Open Notification Center"),
                                 QStringLiteral("label"), [this] { openNotificationCenter(); }));
  layout->addStretch();
  return footer;
}

void NotificationsMenu::refreshItems() {
  try {
    if (!popup_ || !scroll_) return;
    iconLabels_.clear();
    QScrollBar* scrollBar = scroll_->verticalScrollBar();
    const int scrollPosition = scrollBar ? scrollBar->value() : 0;

    auto* contents = new QWidget;
    contents->setProperty("class", QStringLiteral("contents"));
    auto* layout = zeroVBox(contents);
    static const std::optional<std::vector<platform::NotificationItem>> none;
    const auto& items = service_ ? service_->items() : none;
    if (items && !items->empty()) {
      for (const auto& group : groupByApp(*items)) layout->addWidget(createAppSection(group));
    } else {
      layout->addStretch();
      layout->addWidget(createEmptyMessage());
    }
    layout->addStretch();

    // setWidget deletes the old list at once, while a click on one of its buttons may still be
    // running
    if (QWidget* old = scroll_->takeWidget()) old->deleteLater();
    scroll_->setWidget(contents);
    // setWidget turns it back on
    contents->setAutoFillBackground(false);
    popup_->adjustSize();
    popup_->setPosition(options_.alignment, options_.direction, options_.offsetLeft,
                        options_.offsetTop);
    if (scrollBar) scrollBar->setValue(scrollPosition);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Notification menu refresh failed:" << error.what();
  }
}

QWidget* NotificationsMenu::createAppSection(const std::vector<platform::NotificationItem>& items) {
  const QString aumid = items.front().aumid;
  auto* section = new QFrame;
  section->setProperty("class", QStringLiteral("section"));
  auto* layout = zeroVBox(section);
  layout->addWidget(createAppHeader(items.front()));

  const bool expanded = expanded_.contains(aumid);
  const size_t shown = expanded ? items.size() : std::min<size_t>(items.size(), kCollapsedCount);
  for (size_t i = 0; i < shown; ++i) layout->addWidget(createNotificationItem(items[i]));

  const size_t hidden = items.size() - shown;
  if (hidden || (expanded && items.size() > static_cast<size_t>(kCollapsedCount))) {
    const QString text =
        hidden ? QString::number(hidden) + QStringLiteral(" more") : QStringLiteral("Show less");
    layout->addWidget(
        createButton(text, QStringLiteral("more"), [this, aumid] { toggleExpanded(aumid); }));
  }
  return section;
}

QWidget* NotificationsMenu::createAppHeader(const platform::NotificationItem& item) {
  const QString aumid = item.aumid;
  QLabel* clear = createButton(options_.iconDismiss, QStringLiteral("clear-app"),
                               [this, aumid] { clearApp(aumid); });
  auto* header = new HoverFrame(clear);
  header->setProperty("class", QStringLiteral("section-header"));
  auto* layout = zeroHBox(header);

  const QPixmap pixmap = appIcon(aumid);
  if (!pixmap.isNull()) {
    auto* icon = new QLabel;
    icon->setProperty("class", QStringLiteral("icon"));
    icon->setPixmap(pixmap);
    iconLabels_.emplace_back(aumid, icon);
    layout->addWidget(icon);
  }
  auto* name = new QLabel(item.appName.isEmpty() ? QStringLiteral("Other") : item.appName);
  name->setProperty("class", QStringLiteral("name"));
  name->setTextFormat(Qt::PlainText);
  layout->addWidget(name, 1, Qt::AlignVCenter);
  layout->addWidget(clear);
  return header;
}

QWidget* NotificationsMenu::createNotificationItem(const platform::NotificationItem& item) {
  auto* time = new QLabel(shortRelativeTime(item.createdAt, QDateTime::currentDateTimeUtc()));
  time->setProperty("class", QStringLiteral("time"));
  const std::uint32_t id = item.id;
  QLabel* dismissLabel =
      createButton(options_.iconDismiss, QStringLiteral("dismiss"), [this, id] { dismiss(id); });
  auto* card = new NotificationCard([this, item] { activate(item); }, time, dismissLabel);
  card->setProperty("class", QStringLiteral("item"));
  auto* layout = zeroVBox(card);

  if (QWidget* hero = createPicture(item.hero, QStringLiteral("hero"), QSizeF(364, 180)))
    layout->addWidget(hero);

  auto* row = new QHBoxLayout;
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(0);
  layout->addLayout(row);
  if (QWidget* avatar = createAvatar(item)) row->addWidget(avatar, 0, Qt::AlignTop);

  auto* text = new QVBoxLayout;
  text->setContentsMargins(0, 0, 0, 0);
  text->setSpacing(0);
  row->addLayout(text, 1);

  auto* titleRow = new QHBoxLayout;
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(0);
  auto* title = new ElidedLabel;
  title->setFullText(item.title.isEmpty() ? item.appName : item.title);
  title->setProperty("class", QStringLiteral("title"));
  title->setTextFormat(Qt::PlainText);
  // a stylesheet margin gives a label a frame, and a framed label indents its text
  title->setIndent(0);
  titleRow->addWidget(title, 1);
  auto* corner = new QGridLayout;
  corner->setContentsMargins(0, 0, 0, 0);
  for (QLabel* label : {time, dismissLabel})
    corner->addWidget(label, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
  titleRow->addLayout(corner);
  text->addLayout(titleRow);

  if (!item.body.isEmpty()) {
    auto* body = new QLabel(item.body);
    body->setProperty("class", QStringLiteral("body"));
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(true);
    body->setIndent(0);
    // a stylesheet max-height cuts the end of the text, not both ends
    body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    text->addWidget(body);
  }

  if (!item.attribution.isEmpty()) {
    auto* attribution = new ElidedLabel;
    attribution->setFullText(item.attribution);
    attribution->setProperty("class", QStringLiteral("attribution"));
    attribution->setTextFormat(Qt::PlainText);
    attribution->setIndent(0);
    text->addWidget(attribution);
  }

  if (QWidget* picture = createPicture(item.image, QStringLiteral("image")))
    text->addWidget(picture);
  return card;
}

QWidget* NotificationsMenu::createEmptyMessage() {
  auto* empty = new QFrame;
  empty->setProperty("class", QStringLiteral("empty"));
  auto* layout = zeroVBox(empty);

  auto* icon = new QLabel(options_.iconDefault);
  icon->setProperty("class", QStringLiteral("icon"));
  layout->addWidget(icon, 0, Qt::AlignCenter);

  const bool noAccess = service_ && !service_->items().has_value();
  auto* text = new QLabel(noAccess ? QStringLiteral("Notification access is turned off")
                                   : QStringLiteral("No new notifications"));
  text->setProperty("class", QStringLiteral("text"));
  layout->addWidget(text, 0, Qt::AlignCenter);
  if (noAccess) {
    layout->addWidget(createButton(QStringLiteral("Open settings"), QStringLiteral("link"),
                                   [this] { openNotificationSettings(); }),
                      0, Qt::AlignCenter);
  }
  return empty;
}

QLabel* NotificationsMenu::createButton(const QString& text, const QString& cssClass,
                                        std::function<void()> action) {
  return new ButtonLabel(text, cssClass, [action = std::move(action)] {
    try {
      action();
    } catch (const std::exception& error) {
      qWarning().noquote() << "Notification menu action failed:" << error.what();
    }
  });
}

void NotificationsMenu::toggleExpanded(const QString& aumid) {
  if (!expanded_.remove(aumid)) expanded_.insert(aumid);
  refreshItems();
}

void NotificationsMenu::dismiss(std::uint32_t id) {
  if (service_) service_->remove(id);
}

void NotificationsMenu::clearApp(const QString& aumid) {
  if (service_) service_->clearApp(aumid);
}

void NotificationsMenu::clearAll() {
  if (service_) service_->clearAll();
}

void NotificationsMenu::activate(const platform::NotificationItem& item) {
  close();
  if (service_) service_->activate(item);
}

void NotificationsMenu::openNotificationCenter() {
  close();
  if (hooks_.toggleNotificationCenter) hooks_.toggleNotificationCenter();
}

void NotificationsMenu::openNotificationSettings() {
  close();
  if (hooks_.openUri) hooks_.openUri(QString::fromLatin1(kNotificationSettingsUri));
}

void NotificationsMenu::toggleDnd() {
  if (!hooks_.dndStatus || !hooks_.setDndStatus) return;
  const QString status = hooks_.dndStatus() == QLatin1String("disabled")
                             ? QStringLiteral("priority")
                             : QStringLiteral("disabled");
  hooks_.setDndStatus(status);
  updateDndButton(status);
}

void NotificationsMenu::updateDndButton(const QString& status) {
  if (!dnd_) return;
  try {
    const bool enabled = status != QLatin1String("disabled") && status != QLatin1String("unknown");
    dnd_->setText(enabled ? options_.iconDndOn : options_.iconDndOff);
    dnd_->setProperty("class", enabled ? QStringLiteral("dnd active") : QStringLiteral("dnd"));
    if (options_.tooltip) {
      setTooltip(dnd_,
                 enabled ? QStringLiteral("Turn off do not disturb")
                         : QStringLiteral("Turn on do not disturb"),
                 globalState_);
    }
    bar::refreshWidgetStyle(dnd_);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Do Not Disturb update failed:" << error.what();
  }
}

QPixmap NotificationsMenu::appIcon(const QString& aumid) const {
  if (aumid.isEmpty()) return {};
  return icons_.value(aumid);
}

QWidget* NotificationsMenu::createAvatar(const platform::NotificationItem& item) {
  const auto found = pictures_.constFind(
      pictureKey(item.logo, item.logoCircle ? QStringLiteral("round") : QStringLiteral("square")));
  if (found == pictures_.constEnd()) return nullptr;
  auto* avatar = new QLabel;
  avatar->setProperty("class", QStringLiteral("avatar"));
  avatar->setPixmap(*found);
  return avatar;
}

QWidget* NotificationsMenu::createPicture(const QString& path, const QString& cssClass,
                                          const std::optional<QSizeF>& aspect) {
  const auto found = pictures_.constFind(pictureKey(path, QStringLiteral("full")));
  if (found == pictures_.constEnd()) return nullptr;
  auto* picture =
      new ImageView(*found, aspect.value_or(found->deviceIndependentSize()), kPictureMaxHeight);
  picture->setProperty("class", cssClass);
  return picture;
}

void NotificationsMenu::onItemsChanged() {
  try {
    if (!service_) return;
    const auto& items = service_->items();
    loadPictures(items.value_or(std::vector<platform::NotificationItem>{}));
    if (items) loadAppIcons(*items);
    if (isOpen()) refreshItems();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Notification list update failed:" << error.what();
  }
}

void NotificationsMenu::loadPictures(const std::vector<platform::NotificationItem>& items) {
  std::map<QString, std::pair<QString, QString>> wanted;  // key -> (path, shape)
  for (const auto& item : items) {
    if (!item.logo.isEmpty()) {
      const QString shape = item.logoCircle ? QStringLiteral("round") : QStringLiteral("square");
      wanted[pictureKey(item.logo, shape)] = {item.logo, shape};
    }
    for (const QString& path : {item.hero, item.image}) {
      if (!path.isEmpty())
        wanted[pictureKey(path, QStringLiteral("full"))] = {path, QStringLiteral("full")};
    }
  }
  for (auto it = pictures_.begin(); it != pictures_.end();) {
    if (wanted.count(it.key())) {
      ++it;
    } else {
      it = pictures_.erase(it);
    }
  }
  const double ratio = dpr();
  for (const auto& [key, value] : wanted) {
    if (pictures_.contains(key) || picturesLoading_.contains(key)) continue;
    picturesLoading_.insert(key);
    const bool square = value.second != QLatin1String("full");
    const QSize size = square ? QSize(kAvatarSize, kAvatarSize) : QSize(480, 400);
    pool_.start(new ImageLoader(
        &imageSignals_, key, value.first,
        QSize(static_cast<int>(size.width() * ratio), static_cast<int>(size.height() * ratio)),
        square, value.second == QLatin1String("round")));
  }
}

void NotificationsMenu::loadAppIcons(const std::vector<platform::NotificationItem>& items) {
  const int physical = static_cast<int>(options_.appIconSize * dpr());
  for (const auto& item : items) {
    if (item.aumid.isEmpty() || icons_.contains(item.aumid)) continue;
    const QString key = iconKey(item.aumid);
    if (iconsLoading_.contains(key)) continue;
    iconsLoading_.insert(key);
    pool_.start(new AppIconLoader(&imageSignals_, key, item.aumid, physical));
  }
}

void NotificationsMenu::onImageLoaded(const QString& key, const QImage& image) {
  try {
    const bool isIcon = key.startsWith(QLatin1String("icon:"));
    (isIcon ? iconsLoading_ : picturesLoading_).remove(key);
    if (image.isNull()) return;
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr());
    if (isIcon) {
      const QString aumid = key.mid(5);
      // The icon service scales to the configured size; the shell hands back about that size.
      icons_.insert(aumid, pixmap.scaled(QSize(options_.appIconSize, options_.appIconSize) * dpr(),
                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
      icons_[aumid].setDevicePixelRatio(dpr());
      bool shown = false;
      for (auto& [labelAumid, label] : iconLabels_) {
        if (labelAumid != aumid) continue;
        shown = true;
        if (label) label->setPixmap(icons_.value(aumid));
      }
      // Python only updates labels that already have an icon; a header built before the icon
      // arrived gets it on the next refresh, so ask for one.
      if (!shown && isOpen()) refreshTimer_.start();
      return;
    }
    pictures_.insert(key, pixmap);
    if (isOpen()) refreshTimer_.start();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Notification image update failed:" << error.what();
  }
}

}  // namespace yasb::widgets

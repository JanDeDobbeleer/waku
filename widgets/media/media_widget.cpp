#include "widgets/media/media_widget.h"

#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <exception>

#include "core/widgets/backdrop.h"
#include "core/widgets/label_format.h"
#include "core/widgets/polling_worker_registry.h"

namespace yasb::widgets {
namespace {

constexpr int kFullScale = 1000;

// Python str.__getitem__ slicing helper: text[:n] with a possibly negative n.
QString pySliceHead(const QString& text, qsizetype count) {
  const QList<uint> points = text.toUcs4();
  qsizetype n = count;
  const auto size = static_cast<qsizetype>(points.size());
  if (n < 0) n = std::max<qsizetype>(0, size + n);
  n = std::min(n, size);
  return QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()), n);
}

QString joinClass(const char* base, bool enabled) {
  // f"btn play {'disabled' if not enabled else ''}": the trailing space stays when enabled.
  return QString::fromLatin1(base) + QStringLiteral(" ") +
         (enabled ? QString() : QStringLiteral("disabled"));
}

QPainterPath roundedRectPath(const QRectF& r, double radius, bool tl, bool tr, bool br, bool bl) {
  const double rad = std::max(0.0, std::min(radius, std::min(r.width(), r.height()) / 2.0));
  const double d = rad * 2;
  QPainterPath path;
  path.moveTo(r.left() + (tl ? rad : 0), r.top());
  path.lineTo(r.right() - (tr ? rad : 0), r.top());
  if (tr) path.arcTo(r.right() - d, r.top(), d, d, 90, -90);
  path.lineTo(r.right(), r.bottom() - (br ? rad : 0));
  if (br) path.arcTo(r.right() - d, r.bottom() - d, d, d, 0, -90);
  path.lineTo(r.left() + (bl ? rad : 0), r.bottom());
  if (bl) path.arcTo(r.left(), r.bottom() - d, d, d, 270, -90);
  path.lineTo(r.left(), r.top() + (tl ? rad : 0));
  if (tl) path.arcTo(r.left(), r.top(), d, d, 180, -90);
  path.closeSubpath();
  return path;
}

QImage cornerMask(const QSize& size, int alpha, int radius, bool controlsLeft, bool symmetric) {
  bool tl = !controlsLeft;
  bool tr = controlsLeft;
  bool br = controlsLeft;
  bool bl = !controlsLeft;
  if (symmetric) tl = tr = br = bl = true;
  constexpr int scale = 2;
  QImage hr(size * scale, QImage::Format_Grayscale8);
  hr.fill(0);
  QPainter painter(&hr);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(alpha, alpha, alpha));
  painter.drawPath(
      roundedRectPath(QRectF(QPointF(0, 0), QSizeF(hr.size())), radius * scale, tl, tr, br, bl));
  painter.end();
  return hr.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage edgeFadeMask(const QSize& size, int alpha) {
  QImage mask(size, QImage::Format_Grayscale8);
  mask.fill(alpha);
  const int width = size.width();
  const int fade = static_cast<int>(width * 0.3);
  for (int y = 0; y < size.height(); ++y) {
    uchar* row = mask.scanLine(y);
    for (int x = 0; x < fade; ++x) {
      const int value = static_cast<int>(255.0 * (static_cast<double>(x) / fade));
      row[x] = static_cast<uchar>(std::min<int>(row[x], value));
    }
    for (int x = width - fade; x < width; ++x) {
      const int value = static_cast<int>(255.0 * (static_cast<double>(width - x) / fade));
      row[x] = static_cast<uchar>(std::min<int>(row[x], value));
    }
  }
  return mask;
}

}  // namespace

QString cleanMediaLabel(const QString& content, const QMap<QString, QString>& values) {
  struct Token {
    enum Type { Literal, Placeholder, Separator } type;
    QString text;
    QString value;
  };
  static const QRegularExpression pattern(QStringLiteral("(\\{[^{}]*\\}|[^{]+)"));
  std::vector<Token> tokens;
  auto it = pattern.globalMatch(content);
  while (it.hasNext()) {
    const QString match = it.next().captured(0);
    if (match == QLatin1String("{s}")) {
      tokens.push_back({Token::Separator, match, values.value(QStringLiteral("s"))});
    } else if (match.startsWith(u'{') && match.endsWith(u'}')) {
      tokens.push_back({Token::Placeholder, match, values.value(match.mid(1, match.size() - 2))});
    } else {
      tokens.push_back({Token::Literal, match, QString()});
    }
  }
  std::vector<Token> kept;
  for (const Token& token : tokens) {
    if (token.type == Token::Placeholder && token.value.isEmpty()) continue;
    kept.push_back(token);
  }
  QString result;
  for (size_t i = 0; i < kept.size(); ++i) {
    if (kept[i].type == Token::Separator) {
      bool left = false;
      bool right = false;
      for (size_t j = 0; j < kept.size(); ++j) {
        if (kept[j].type != Token::Placeholder) continue;
        if (j < i) left = true;
        if (j > i) right = true;
      }
      if (!(left && right)) continue;
    }
    result += kept[i].text;
  }
  return result;
}

QString truncateMediaField(const QString& text, int maxSize) {
  if (text.toUcs4().size() > maxSize) return pySliceHead(text, maxSize - 3) + QStringLiteral("...");
  return text;
}

QString formatMediaTime(double seconds) {
  const long long total = static_cast<long long>(seconds);  // int(): truncates toward zero
  auto floorDiv = [](long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
  };
  const long long mins = floorDiv(total, 60);
  const long long secs = total - mins * 60;
  const long long hours = floorDiv(mins, 60);
  const long long minutes = mins - hours * 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QImage renderBarThumbnail(const QImage& source, const ThumbnailStyle& style) {
  if (source.isNull() || source.height() <= 0) return {};
  const int availableWidth = style.labelWidth + style.paddingAdded;
  const double aspect = static_cast<double>(source.width()) / source.height();
  const int newHeight = static_cast<int>(availableWidth / aspect);
  if (availableWidth <= 0 || newHeight <= 0) return {};
  QImage image =
      source.convertToFormat(QImage::Format_RGBA8888)
          .scaled(availableWidth, newHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const int availableHeight = std::max(1, style.containerHeight);
  if (image.height() > availableHeight) {
    const int y1 = (image.height() - availableHeight) / 2;
    image = image.copy(0, y1, image.width(), availableHeight);
  }
  QImage mask(image.size(), QImage::Format_Grayscale8);
  mask.fill(style.alpha);
  if (style.edgeFade) {
    mask = edgeFadeMask(image.size(), style.alpha);
  } else if (style.cornerRadius > 0) {
    mask = cornerMask(image.size(), style.alpha, style.cornerRadius, style.controlsLeft,
                      style.symmetric);
  }
  for (int y = 0; y < image.height(); ++y) {
    uchar* row = image.scanLine(y);
    const uchar* alphaRow = mask.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) row[x * 4 + 3] = alphaRow[x];
  }
  return image;
}

QImage renderPopupThumbnail(const QImage& source, int size) {
  if (source.isNull() || source.height() <= 0 || size <= 0) return {};
  const double aspect = static_cast<double>(source.width()) / source.height();
  int newWidth = size;
  int newHeight = size;
  if (aspect > 1) {
    newHeight = size;
    newWidth = static_cast<int>(size * aspect);
  } else {
    newWidth = size;
    newHeight = static_cast<int>(size / aspect);
  }
  if (newWidth <= 0 || newHeight <= 0) return {};
  QImage resized =
      source.convertToFormat(QImage::Format_RGBA8888)
          .scaled(newWidth, newHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (resized.width() >= size && resized.height() >= size) {
    return resized.copy((resized.width() - size) / 2, (resized.height() - size) / 2, size, size);
  }
  return resized.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// --- small widgets ------------------------------------------------------------------------------

void MediaButton::mousePressEvent(QMouseEvent* event) { event->accept(); }

void MediaButton::mouseReleaseEvent(QMouseEvent* event) {
  const QStringList classes = property("class").toString().split(u' ', Qt::SkipEmptyParts);
  if (classes.contains(QStringLiteral("disabled"))) {
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && action) {
    try {
      action();
    } catch (const std::exception& error) {
      qWarning().noquote() << "Media button action failed:" << error.what();
    }
  }
  event->accept();
}

void MediaThumbnailLabel::paintEvent(QPaintEvent* event) {
  const QPixmap pix = pixmap();
  if (pix.isNull() || radius_ <= 0) {
    QLabel::paintEvent(event);
    return;
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF rect(contentsRect());
  const double radius =
      std::min(static_cast<double>(radius_), std::min(rect.width(), rect.height()) / 2.0);
  QPainterPath path;
  path.addRoundedRect(rect, radius, radius);
  painter.setClipPath(path);
  painter.drawPixmap(contentsRect(), pix, pix.rect());
}

MediaMenuPopup::MediaMenuPopup(QWidget* anchor, bar::GlobalState& globalState, bool blur,
                               bool roundCorners, const QString& roundCornersType,
                               const QString& borderColor)
    : PopupWidget(anchor),
      blur_(blur),
      roundCorners_(roundCorners),
      roundCornersType_(roundCornersType),
      borderColor_(borderColor),
      globalState_(globalState) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                 Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_DeleteOnClose);
}

void MediaMenuPopup::showEvent(QShowEvent* event) {
  PopupWidget::showEvent(event);
  if (blur_)
    enableBlur(winId(), globalState_.isDark(), roundCorners_, roundCornersType_, borderColor_);
}

namespace {

// WheelEventFilter: wheel over the flyout switches the media session, then rebuilds the flyout.
class WheelFilter final : public QObject {
 public:
  WheelFilter(MediaWidget* widget, platform::MediaSessionService* service, QWidget* dialog)
      : QObject(dialog), widget_(widget), service_(service), dialog_(dialog) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    Q_UNUSED(watched);
    if (event->type() != QEvent::Wheel || !service_ || !widget_) return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (!dialog_->geometry().contains(wheel->globalPosition().toPoint())) return false;
    const auto* before = service_->currentSession();
    const QString oldId = before ? before->appId : QString();
    const bool hadOld = before != nullptr;
    if (wheel->angleDelta().y() > 0) {
      service_->switchCurrentSession(+1);
    } else if (wheel->angleDelta().y() < 0) {
      service_->switchCurrentSession(-1);
    }
    const auto* after = service_->currentSession();
    const bool changed = (after != nullptr) != hadOld || (after && after->appId != oldId);
    if (changed) {
      QPointer<MediaWidget> widget = widget_;
      dialog_->hide();
      if (widget) widget->showMenu();
    }
    return true;
  }

 private:
  QPointer<MediaWidget> widget_;
  QPointer<platform::MediaSessionService> service_;
  QWidget* dialog_;
};

}  // namespace

// --- MediaWidget --------------------------------------------------------------------------------

MediaWidget::MediaWidget(const WidgetContext& context, platform::MediaSessionService* service)
    : BaseWidget(
          QString::fromStdString(context.name), context.eventBus,
          QStringLiteral("media-widget ") +
              QString::fromStdString(
                  std::get<config::options::MediaWidgetConfig>(context.typedOptions).class_name),
          0),
      options_(std::get<config::options::MediaWidgetConfig>(context.typedOptions)),
      globalState_(context.globalState),
      service_(service) {
  if (options_.hide_empty) hide();

  // The media.png fallback is a Qt resource compiled into yasb_widgets.
  Q_INIT_RESOURCE(media);
  {
    QImage fallback(QStringLiteral(":/yasb/media/media.png"));
    if (!fallback.isNull()) {
      const int size = options_.media_menu.thumbnail_size;
      emptyThumb_ = QPixmap::fromImage(
          fallback.convertToFormat(QImage::Format_RGBA8888)
              .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
  }

  QLayout* containerLayout = labelContainer()->layout();
  auto* thumbnailStack = new QGridLayout;
  thumbnailStack->setContentsMargins(0, 0, 0, 0);
  thumbnailStack->setSpacing(0);

  auto createButtons = [&] {
    if (options_.controls_hide) return;
    prev_ = createMediaButton(QString::fromStdString(options_.icons.prev_track), [this] {
      if (service_) service_->previous();
    });
    play_ = createMediaButton(QString::fromStdString(options_.icons.play), [this] {
      if (service_) service_->playPause();
    });
    next_ = createMediaButton(QString::fromStdString(options_.icons.next_track), [this] {
      if (service_) service_->next();
    });
  };
  if (options_.controls_left) {
    createButtons();
    if (!options_.controls_only)
      static_cast<QBoxLayout*>(containerLayout)->addLayout(thumbnailStack);
  } else {
    if (!options_.controls_only)
      static_cast<QBoxLayout*>(containerLayout)->addLayout(thumbnailStack);
    createButtons();
  }

  label_ = new QLabel(this);
  label_->setAlignment(Qt::AlignCenter);
  labelAlt_ = new QLabel(this);
  labelAlt_->setAlignment(Qt::AlignCenter);
  thumbnailLabel_ = new QLabel(this);
  thumbnailLabel_->setAlignment(Qt::AlignCenter);
  label_->setProperty("class", QStringLiteral("label"));
  labelAlt_->setProperty("class", QStringLiteral("label alt"));

  auto* progressContainer = new QFrame;
  progressContainer->setContentsMargins(0, 0, 0, 0);
  progressContainer->setMinimumWidth(0);
  progressContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
  progressContainer->setAttribute(Qt::WA_TransparentForMouseEvents);
  progressContainer_ = progressContainer;

  auto* progressLayout = new QHBoxLayout(progressContainer);
  const QString alignment = QString::fromStdString(options_.progress_bar.alignment);
  progressLayout->setAlignment(alignment == QLatin1String("top")      ? Qt::AlignTop
                               : alignment == QLatin1String("center") ? Qt::AlignVCenter
                                                                      : Qt::AlignBottom);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(0);

  progressBar_ = new QProgressBar;
  progressBar_->setMinimumWidth(0);
  progressBar_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
  progressBar_->setProperty("class", QStringLiteral("progress-bar"));
  progressBar_->setRange(0, kFullScale);
  progressBar_->setTextVisible(false);
  progressBar_->setValue(500);
  progressLayout->addWidget(progressBar_);

  thumbnailStack->addWidget(thumbnailLabel_, 0, 0);
  thumbnailStack->addWidget(progressContainer, 0, 0);
  thumbnailStack->addWidget(labelAlt_, 0, 0);
  thumbnailStack->addWidget(label_, 0, 0);

  if (options_.controls_only) {
    label_->hide();
    labelAlt_->hide();
    thumbnailLabel_->hide();
    progressContainer->hide();
  }
  if (!options_.progress_bar.enabled) progressContainer->hide();

  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("do_nothing")});
  registerCallback(QStringLiteral("toggle_media_menu"), [this](const QStringList&) { showMenu(); });
  if (!options_.controls_only) {
    registerCallback(QStringLiteral("toggle_play_pause"),
                     [this](const QStringList&) { togglePlayPause(); });
    registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
    label_->show();
  }
  registerCallback(QStringLiteral("open_media_source"),
                   [this](const QStringList&) { openMediaSource(); });

  labelAlt_->hide();

  if (service_) {
    connect(service_, &platform::MediaSessionService::mediaDataChanged, this,
            [this] { onMediaDataChanged(); });
    connect(service_, &platform::MediaSessionService::currentSessionChanged, this,
            [this] { onSessionStatusChanged(); });
    connect(service_, &platform::MediaSessionService::mediaPropertiesChanged, this,
            [this] { onMediaPropertiesChanged(); });
    connect(service_, &platform::MediaSessionService::timelineInfoChanged, this,
            [this] { onTimelineChanged(); });
    connect(service_, &platform::MediaSessionService::playbackInfoChanged, this,
            [this] { onPlaybackInfoChanged(); });
    // A widget built after the service already has sessions would otherwise wait for an event.
    if (service_->currentSession()) {
      onMediaDataChanged();
      onSessionStatusChanged();
    }
  }
}

QLabel* MediaWidget::prevButton() const { return prev_; }
QLabel* MediaWidget::playButton() const { return play_; }
QLabel* MediaWidget::nextButton() const { return next_; }
MediaMenuPopup* MediaWidget::menu() const { return dialog_; }

const platform::MediaSessionState* MediaWidget::current() const {
  return service_ ? service_->currentSession() : nullptr;
}

MediaButton* MediaWidget::createMediaButton(const QString& icon, std::function<void()> action) {
  auto* button = new MediaButton(this);
  button->setProperty("class", QStringLiteral("btn disabled"));
  button->setAlignment(Qt::AlignCenter);
  button->setText(icon);
  button->action = std::move(action);
  labelContainer()->layout()->addWidget(button);
  return button;
}

QString MediaWidget::formatMaxFieldSize(const QString& text, const char* fieldType) const {
  const QLatin1String type{fieldType};
  int maxSize = 0;
  if (type == QLatin1String("popup_title")) {
    maxSize = options_.media_menu.max_title_size;
  } else if (type == QLatin1String("popup_artist")) {
    maxSize = options_.media_menu.max_artist_size;
  } else if (type == QLatin1String("popup_source")) {
    maxSize = options_.media_menu.max_source_size;
  } else {
    // A scrolling label shows the original text.
    if (options_.scrolling_label.enabled) return text;
    maxSize = showAltLabel_ ? options_.max_field_size.label_alt : options_.max_field_size.label;
  }
  return truncateMediaField(text, maxSize);
}

void MediaWidget::applyButtonState(QLabel* button, const char* base, bool enabled) const {
  if (!button) return;
  button->setProperty("class", joinClass(base, enabled));
  bar::refreshWidgetStyle(button);
}

void MediaWidget::onMediaDataChanged() {
  const platform::MediaSessionState* session = current();
  const bool changed = session && (!hadSession_ || session->appId != lastAppId_);
  hadSession_ = session != nullptr;
  lastAppId_ = session ? session->appId : QString();
  updateInterpolatedPosition();
  if (changed) {
    onMediaPropertiesChanged();
    onPlaybackInfoChanged();
  }
}

void MediaWidget::updateInterpolatedPosition() {
  const platform::MediaSessionState* session = current();
  if (!session) return;
  try {
    if (session->timelineEnabled && session->duration > 0 &&
        session->duration < kMaxTimelineDuration) {
      progressBar_->setHidden(false);
      const int value = std::min(
          kFullScale, static_cast<int>(session->currentPos / session->duration * kFullScale));
      if (progressBar_->value() != value) progressBar_->setValue(value);
    } else {
      progressBar_->setHidden(true);
    }

    if (!(dialog_ && dialog_->isVisible()) || seeking_) return;
    if (popupCurrentTime_) popupCurrentTime_->setText(formatMediaTime(session->currentPos));
    if (progressSlider_ && session->duration > 0) {
      const int percent = std::min(
          kFullScale, static_cast<int>(session->currentPos / session->duration * kFullScale));
      // Skip sub-0.5% moves so the slider does not look jumpy.
      if (std::abs(percent - progressSlider_->value()) >= 5) progressSlider_->setValue(percent);
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error updating interpolated position:" << error.what();
  }
}

void MediaWidget::onTimelineChanged() {
  const platform::MediaSessionState* session = current();
  if (!session) return;
  if (popupCurrentTime_) popupCurrentTime_->setText(formatMediaTime(session->currentPos));
  if (popupTotalTime_) popupTotalTime_->setText(formatMediaTime(session->duration));
}

void MediaWidget::onSessionStatusChanged() {
  QLabel* active = showAltLabel_ ? labelAlt_ : label_;
  if (current()) {
    show();
    if (!options_.controls_only) active->show();
    return;
  }
  thumbnailLabel_->hide();
  active->hide();
  active->setText(QString());
  if (!options_.controls_hide) {
    if (play_) {
      play_->setText(QString::fromStdString(options_.icons.play));
      play_->setProperty("class", QStringLiteral("btn play disabled"));
      bar::refreshWidgetStyle(play_);
    }
    if (prev_) {
      prev_->setProperty("class", QStringLiteral("btn prev disabled"));
      bar::refreshWidgetStyle(prev_);
    }
    if (next_) {
      next_->setProperty("class", QStringLiteral("btn next disabled"));
      bar::refreshWidgetStyle(next_);
    }
  }
  if (options_.hide_empty) hide();
}

void MediaWidget::onPlaybackInfoChanged() {
  const platform::MediaSessionState* session = current();
  if (!session || !session->playbackReady) return;
  const bool playing = session->isPlaying;
  const bool prevEnabled = session->prevEnabled;
  const bool playEnabled = session->playEnabled;
  const bool nextEnabled = session->nextEnabled;
  isPlaying_ = playing;

  if (!options_.controls_hide) {
    const std::string& icon = playing ? options_.icons.pause : options_.icons.play;
    if (play_) {
      play_->setText(QString::fromStdString(icon));
      applyButtonState(play_, "btn play", playEnabled);
      play_->setStyleSheet(QString());
    }
    if (prev_) {
      applyButtonState(prev_, "btn prev", prevEnabled);
      prev_->setStyleSheet(QString());
    }
    if (next_) {
      applyButtonState(next_, "btn next", nextEnabled);
      next_->setStyleSheet(QString());
    }
  }

  if (dialog_ && dialog_->isVisible()) {
    if (popupPlay_) {
      popupPlay_->setText(QString::fromStdString(playing ? options_.media_menu_icons.pause
                                                         : options_.media_menu_icons.play));
      applyButtonState(popupPlay_, "btn play", playEnabled);
    }
    if (popupPrev_) applyButtonState(popupPrev_, "btn prev", prevEnabled);
    if (popupNext_) applyButtonState(popupNext_, "btn next", nextEnabled);
  }
}

void MediaWidget::updatePopupMenuButtons() {
  const platform::MediaSessionState* session = current();
  bool prevEnabled = true;
  bool nextEnabled = true;
  bool playEnabled = true;
  if (session && session->playbackReady) {
    prevEnabled = session->prevEnabled;
    nextEnabled = session->nextEnabled;
    playEnabled = session->playEnabled;
  }
  if (popupPlay_) {
    popupPlay_->setText(QString::fromStdString(isPlaying_ ? options_.media_menu_icons.pause
                                                          : options_.media_menu_icons.play));
    applyButtonState(popupPlay_, "btn play", playEnabled);
  }
  if (popupPrev_) applyButtonState(popupPrev_, "btn prev", prevEnabled);
  if (popupNext_) applyButtonState(popupNext_, "btn next", nextEnabled);
}

QPixmap MediaWidget::popupThumbnailFor(const platform::MediaSessionState& session) const {
  if (!session.thumbnail.isEmpty()) {
    const QImage image = renderPopupThumbnail(QImage::fromData(session.thumbnail),
                                              options_.media_menu.thumbnail_size);
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
  }
  return emptyThumb_;
}

void MediaWidget::updatePopupProperties(const platform::MediaSessionState& session) {
  if (popupTitle_ && popupArtist_ && popupThumbnail_) {
    popupTitle_->setText(formatMaxFieldSize(session.title, "popup_title"));
    popupArtist_->setText(formatMaxFieldSize(session.artist, "popup_artist"));
    popupThumbnail_->setPixmap(popupThumbnailFor(session));
  }
  if (popupSource_ && !session.sourceName.isEmpty()) {
    popupSource_->setText(formatMaxFieldSize(session.sourceName, "popup_source"));
    popupSource_->setProperty(
        "class", QStringLiteral("source ") + platform::sourceAppClassName(session.sourceName));
    bar::refreshWidgetStyle(popupSource_);
  }
}

void MediaWidget::onMediaPropertiesChanged() {
  const platform::MediaSessionState* session = current();
  try {
    if (session && dialog_ && dialog_->isVisible()) updatePopupProperties(*session);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error updating popup content:" << error.what();
  }

  QLabel* active = showAltLabel_ ? labelAlt_ : label_;
  const std::string& content = showAltLabel_ ? options_.label_alt : options_.label;

  if (options_.controls_only) return;

  if (session) {
    QString formatted;
    try {
      QMap<QString, QString> info;
      info.insert(QStringLiteral("s"), QString::fromStdString(options_.separator));
      info.insert(QStringLiteral("title"), formatMaxFieldSize(session->title));
      info.insert(QStringLiteral("artist"), formatMaxFieldSize(session->artist));
      const QString cleaned = cleanMediaLabel(QString::fromStdString(content), info);
      nlohmann::json kwargs = nlohmann::json::object();
      for (auto it = info.cbegin(); it != info.cend(); ++it)
        kwargs[it.key().toStdString()] = it.value().toStdString();
      formatted = QString::fromStdString(pyFormat(cleaned.toStdString(), kwargs));
      if (options_.max_field_size.truncate_whole_label) formatted = formatMaxFieldSize(formatted);
    } catch (const std::exception& error) {
      qWarning().noquote() << "Error formatting label:" << error.what();
      formatted = session->title.isEmpty() ? QStringLiteral("No media")
                                           : formatMaxFieldSize(session->title);
    }
    active->setText(formatted);
  }

  if (!options_.show_thumbnail) return;

  if (session && session->thumbnail.isEmpty()) {
    thumbnailLabel_->hide();
    return;
  }
  bool failed = false;
  try {
    if (session && !session->title.isEmpty() && !session->thumbnail.isEmpty()) {
      ThumbnailStyle style;
      style.labelWidth = active->sizeHint().width();
      style.paddingAdded = options_.scrolling_label.enabled ? 0 : options_.thumbnail_padding;
      style.containerHeight = labelContainer()->contentsRect().height();
      style.alpha = options_.thumbnail_alpha;
      style.cornerRadius = options_.thumbnail_corner_radius;
      style.controlsLeft = options_.controls_left;
      style.symmetric = options_.symmetric_corner_radius;
      style.edgeFade = options_.thumbnail_edge_fade;
      const QImage image = renderBarThumbnail(QImage::fromData(session->thumbnail), style);
      if (image.isNull()) {
        failed = true;
      } else {
        thumbnailLabel_->setPixmap(QPixmap::fromImage(image));
      }
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error setting thumbnail:" << error.what();
    failed = true;
  }
  if (failed) {
    thumbnailLabel_->hide();
  } else {
    thumbnailLabel_->show();
  }
}

void MediaWidget::toggleLabel() {
  showAltLabel_ = !showAltLabel_;
  if (showAltLabel_) {
    label_->hide();
    labelAlt_->show();
  } else {
    label_->show();
    labelAlt_->hide();
  }
  // Re-render for the other label (and its thumbnail width).
  if (service_) service_->forceUpdate();
}

void MediaWidget::togglePlayPause() {
  if (service_) service_->playPause();
}

void MediaWidget::openMediaSource() {
  const platform::MediaSessionState* session = current();
  if (session && !session->appId.isEmpty()) platform::activateAppByAumid(session->appId);
}

void MediaWidget::wheelEvent(QWheelEvent* event) {
  if (!service_) return;
  if (event->angleDelta().y() > 0) {
    service_->switchCurrentSession(+1);
  } else if (event->angleDelta().y() < 0) {
    service_->switchCurrentSession(-1);
  }
}

void MediaWidget::onSliderReleased() {
  if (!progressSlider_) return;
  const platform::MediaSessionState* session = current();
  if (session && session->duration > 0 && service_) {
    service_->seek(progressSlider_->value() / 1000.0 * session->duration);
  }
  seeking_ = false;
}

void MediaWidget::onSliderValueChanged(int value) {
  if (!seeking_) return;
  const platform::MediaSessionState* session = current();
  if (!session || session->duration <= 0) return;
  if (popupCurrentTime_)
    popupCurrentTime_->setText(formatMediaTime(value / 1000.0 * session->duration));
  if (popupTotalTime_) popupTotalTime_->setText(formatMediaTime(session->duration));
}

void MediaWidget::showMenu() {
  if (dialog_) dialog_->close();
  const auto& menuOptions = options_.media_menu;
  auto* dialog = new MediaMenuPopup(this, globalState_, menuOptions.blur, menuOptions.round_corners,
                                    QString::fromStdString(menuOptions.round_corners_type),
                                    QString::fromStdString(menuOptions.border_color));
  dialog_ = dialog;
  dialog->setProperty("class", QStringLiteral("media-menu"));

  auto* mainLayout = new QVBoxLayout(dialog);
  mainLayout->setContentsMargins(12, 12, 12, 12);
  mainLayout->setSpacing(0);
  auto* contentLayout = new QHBoxLayout;
  contentLayout->setSpacing(0);

  const platform::MediaSessionState* session = current();
  popupTitle_.clear();
  popupArtist_.clear();
  popupSource_.clear();
  popupThumbnail_.clear();
  popupPrev_.clear();
  popupPlay_.clear();
  popupNext_.clear();
  if (session) {
    auto* thumbnail = new MediaThumbnailLabel(dialog, menuOptions.thumbnail_corner_radius);
    popupThumbnail_ = thumbnail;
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setProperty("class", QStringLiteral("thumbnail"));
    thumbnail->setContentsMargins(0, 0, 0, 0);
    thumbnail->action = [this] { openMediaSource(); };
    thumbnail->setFixedSize(menuOptions.thumbnail_size, menuOptions.thumbnail_size);
    try {
      const QPixmap pixmap = popupThumbnailFor(*session);
      if (!pixmap.isNull()) {
        thumbnail->setPixmap(pixmap);
        contentLayout->addWidget(thumbnail, 0, Qt::AlignTop);
      }

      auto* textLayout = new QVBoxLayout;
      textLayout->setContentsMargins(0, 0, 0, 0);
      textLayout->setSpacing(0);

      auto* title =
          new QLabel(session->title.isEmpty() ? QStringLiteral("Unknown Title")
                                              : formatMaxFieldSize(session->title, "popup_title"));
      popupTitle_ = title;
      title->setContentsMargins(0, 0, 0, 0);
      title->setProperty("class", QStringLiteral("title"));
      title->setWordWrap(true);
      title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

      auto* artist = new QLabel(session->artist.isEmpty()
                                    ? QString()
                                    : formatMaxFieldSize(session->artist, "popup_artist"));
      popupArtist_ = artist;
      artist->setContentsMargins(0, 0, 0, 0);
      artist->setProperty("class", QStringLiteral("artist"));
      artist->setWordWrap(true);
      artist->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
      textLayout->addWidget(title, 0, Qt::AlignTop);
      textLayout->addWidget(artist, 0, Qt::AlignTop);

      auto* controls = new QHBoxLayout;
      controls->setSpacing(0);
      auto makeButton = [&](const char* cls, const std::string& icon,
                            std::function<void()> action) {
        auto* button = new MediaButton(dialog);
        button->setProperty("class", QString::fromLatin1(cls));
        button->setAlignment(Qt::AlignCenter);
        button->setText(QString::fromStdString(icon));
        button->action = std::move(action);
        return button;
      };
      MediaButton* prev = makeButton("btn prev", options_.media_menu_icons.prev_track, [this] {
        if (service_) service_->previous();
      });
      popupPrev_ = prev;
      MediaButton* play = makeButton(
          "btn play", isPlaying_ ? options_.media_menu_icons.pause : options_.media_menu_icons.play,
          [this] {
            if (service_) service_->playPause();
          });
      popupPlay_ = play;
      MediaButton* next = makeButton("btn next", options_.media_menu_icons.next_track, [this] {
        if (service_) service_->next();
      });
      popupNext_ = next;
      controls->addWidget(prev);
      controls->addWidget(play);
      controls->addWidget(next);
      controls->addStretch(1);

      const QString sourceName = session->sourceName;
      if (!sourceName.isEmpty() && menuOptions.show_source) {
        auto* source = new QLabel(formatMaxFieldSize(sourceName, "popup_source"));
        popupSource_ = source;
        source->setContentsMargins(0, 0, 0, 0);
        source->setProperty("class",
                            QStringLiteral("source ") + platform::sourceAppClassName(sourceName));
        source->setAlignment(Qt::AlignCenter);
        source->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        controls->addWidget(source, 0, Qt::AlignVCenter);
      }
      textLayout->addLayout(controls);
      contentLayout->addLayout(textLayout);
    } catch (const std::exception& error) {
      qWarning().noquote() << "Error setting thumbnail in menu:" << error.what();
    }
  } else {
    auto* noMedia = new QLabel(QStringLiteral("No media playing"));
    noMedia->setProperty("class", QStringLiteral("no-media"));
    noMedia->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(noMedia);
  }
  mainLayout->addLayout(contentLayout);

  auto* timeContainer = new QFrame;
  timeContainer->setProperty("class", QStringLiteral("media-timeline-container"));
  timeContainer->setContentsMargins(0, 0, 0, 0);
  auto* timeLayout = new QVBoxLayout(timeContainer);
  timeLayout->setContentsMargins(0, 0, 0, 0);
  timeLayout->setSpacing(0);

  auto* slider = new QSlider(Qt::Horizontal);
  progressSlider_ = slider;
  slider->setProperty("class", QStringLiteral("progress-slider"));
  slider->setMinimum(0);
  slider->setMaximum(kFullScale);

  auto* timeLabels = new QHBoxLayout;
  timeLabels->setContentsMargins(0, 0, 0, 0);
  timeLabels->setSpacing(0);
  auto* currentTime = new QLabel(QStringLiteral("00:00"));
  popupCurrentTime_ = currentTime;
  currentTime->setProperty("class", QStringLiteral("playback-time current"));
  currentTime->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  auto* totalTime = new QLabel(QStringLiteral("00:00"));
  popupTotalTime_ = totalTime;
  totalTime->setProperty("class", QStringLiteral("playback-time total"));
  totalTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  if (session) {
    currentTime->setText(formatMediaTime(session->currentPos));
    totalTime->setText(formatMediaTime(session->duration));
  }
  timeLabels->addWidget(currentTime);
  timeLabels->addStretch(1);
  timeLabels->addWidget(totalTime);
  timeLayout->addLayout(timeLabels);
  timeLayout->addWidget(slider);
  mainLayout->addWidget(timeContainer);

  if (session && session->duration > 0) {
    slider->setValue(std::min(
        kFullScale, static_cast<int>(session->currentPos / session->duration * kFullScale)));
  } else {
    slider->setValue(0);
  }
  connect(slider, &QSlider::sliderPressed, this, [this] { seeking_ = true; });
  connect(slider, &QSlider::sliderReleased, this, [this] { onSliderReleased(); });
  connect(slider, &QSlider::valueChanged, this, [this](int value) { onSliderValueChanged(value); });
  seeking_ = false;

  if (!(session && session->timelineEnabled && session->duration > 0 &&
        session->duration < kMaxTimelineDuration)) {
    timeContainer->setVisible(false);
    QTimer::singleShot(0, dialog, [dialog] { dialog->adjustSize(); });
  }

  dialog->adjustSize();
  dialog->setPosition(QString::fromStdString(menuOptions.alignment),
                      QString::fromStdString(menuOptions.direction), menuOptions.offset_left,
                      menuOptions.offset_top);
  updatePopupMenuButtons();
  dialog->show();
  if (service_) dialog->installEventFilter(new WheelFilter(this, service_, dialog));
}

void registerMediaWidgets(WidgetRegistry& registry) {
  registry.add("yasb.media.MediaWidget", [](const WidgetContext& context) {
    auto* service = context.pollingWorkers.service<platform::MediaSessionService>("media", [] {
      auto* created = new platform::MediaSessionService(platform::createSystemMediaSource());
      created->start();
      return created;
    });
    return new MediaWidget(context, service);
  });
}

}  // namespace yasb::widgets

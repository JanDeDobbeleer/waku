#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResource>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "widgets/control_center/control_center_sections.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

// _MediaButton: a label that runs its action on a left release.
class MediaButton final : public QLabel {
 public:
  explicit MediaButton(QWidget* parent) : QLabel(parent) { setAlignment(Qt::AlignCenter); }
  std::function<void()> action;

 protected:
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && action) {
      try {
        action();
      } catch (const std::exception& error) {
        qWarning().noquote() << "Error executing media control:" << error.what();
      }
      event->accept();
      return;
    }
    QLabel::mouseReleaseEvent(event);
  }
};

}  // namespace

MediaSection::MediaSection(QWidget* parent, const config::options::MediaSectionConfig& options,
                           SectionContext context)
    : QFrame(parent), options_(options), context_(std::move(context)) {
  setProperty("class", "section media");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  hide();

  // The media.png fallback is a Qt resource compiled into yasb_widgets.
  Q_INIT_RESOURCE(media);
  thumbDpr_ = devicePixelRatioF();
  {
    const QImage fallback(QStringLiteral(":/yasb/media/media.png"));
    if (!fallback.isNull()) emptyThumbnail_ = roundedThumbnail(fallback);
  }

  hideTimer_.setSingleShot(true);
  hideTimer_.setInterval(1000);
  connect(&hideTimer_, &QTimer::timeout, this, [this] { hideNow(); });

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* content = new QFrame(this);
  auto* contentLayout = new QHBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  thumbnail_ = new QLabel(content);
  thumbnail_->setProperty("class", "thumbnail");
  thumbnail_->setFixedSize(options_.thumbnail_size, options_.thumbnail_size);
  thumbnail_->setAlignment(Qt::AlignCenter);

  auto* trackInfo = new QFrame(content);
  trackInfo->setProperty("class", "track-info");
  trackInfo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto* trackLayout = new QVBoxLayout(trackInfo);
  trackLayout->setContentsMargins(0, 0, 0, 0);
  trackLayout->setSpacing(0);

  title_ = new ElidedLabel(trackInfo);
  title_->setProperty("class", "title");
  title_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  artist_ = new ElidedLabel(trackInfo);
  artist_->setProperty("class", "subtext");
  artist_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  trackLayout->addWidget(title_);
  trackLayout->addWidget(artist_);
  trackLayout->addStretch(1);

  auto* controls = new QFrame(content);
  controls->setProperty("class", "controls");
  controls->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  auto* controlsLayout = new QHBoxLayout(controls);
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(0);

  auto* prev = new MediaButton(controls);
  prev->setProperty("class", "button prev");
  prev->setText(qs(options_.icons.prev_track));
  prev->action = [this] {
    if (context_.system.media) context_.system.media->previous();
  };
  auto* play = new MediaButton(controls);
  play->setProperty("class", "button play");
  play->action = [this] {
    if (context_.system.media) context_.system.media->playPause();
  };
  auto* next = new MediaButton(controls);
  next->setProperty("class", "button next");
  next->setText(qs(options_.icons.next_track));
  next->action = [this] {
    if (context_.system.media) context_.system.media->next();
  };
  prev_ = prev;
  play_ = play;
  next_ = next;
  controlsLayout->addWidget(prev_);
  controlsLayout->addWidget(play_);
  controlsLayout->addWidget(next_);

  contentLayout->addWidget(thumbnail_);
  contentLayout->addWidget(trackInfo, 1);
  contentLayout->addWidget(controls);
  layout->addWidget(content);

  if (platform::MediaSessionService* media = context_.system.media) {
    connect(media, &platform::MediaSessionService::currentSessionChanged, this,
            [this] { syncUi(true); });
    connect(media, &platform::MediaSessionService::mediaPropertiesChanged, this,
            [this] { syncUi(true); });
    connect(media, &platform::MediaSessionService::playbackInfoChanged, this,
            [this] { updateControls(); });
  }
}

bool MediaSection::event(QEvent* event) {
  // Built before the popup has a screen, so the scale is only known once it is shown.
  if (event->type() == QEvent::Show || event->type() == QEvent::DevicePixelRatioChange)
    updateThumbnailScale();
  return QFrame::event(event);
}

void MediaSection::updateThumbnailScale() {
  const qreal dpr = devicePixelRatioF();
  if (dpr == thumbDpr_) return;
  thumbDpr_ = dpr;
  const QImage fallback(QStringLiteral(":/yasb/media/media.png"));
  emptyThumbnail_ = fallback.isNull() ? QPixmap() : roundedThumbnail(fallback);
  thumbnailBytes_.clear();
  platform::MediaSessionService* media = context_.system.media;
  if (const auto* session = media ? media->currentSession() : nullptr) applyThumbnail(session);
}

void MediaSection::hideNow() {
  platform::MediaSessionService* media = context_.system.media;
  if (media && media->currentSession()) return;
  currentAppId_.clear();
  currentTitle_.clear();
  currentArtist_.clear();
  currentPlaying_.reset();
  hide();
}

void MediaSection::refreshState() { syncUi(true); }

void MediaSection::syncUi(bool forceThumbnail) {
  platform::MediaSessionService* media = context_.system.media;
  const platform::MediaSessionState* session = media ? media->currentSession() : nullptr;
  if (!session) {
    if (!isHidden() && !hideTimer_.isActive()) hideTimer_.start();
    return;
  }
  hideTimer_.stop();
  if (isHidden()) show();

  const bool sessionChanged = session->appId != currentAppId_;
  currentAppId_ = session->appId;

  const QString title = session->title.isEmpty() ? QStringLiteral("Unknown Title") : session->title;
  const QString artist =
      session->artist.isEmpty() ? QStringLiteral("Unknown Artist") : session->artist;
  if (title != currentTitle_) {
    currentTitle_ = title;
    title_->setFullText(title);
  }
  if (artist != currentArtist_) {
    currentArtist_ = artist;
    artist_->setFullText(artist);
  }
  if (sessionChanged || forceThumbnail) applyThumbnail(session);
  updateControlsFrom(*session);
}

void MediaSection::applyThumbnail(const platform::MediaSessionState* session) {
  if (!session || session->thumbnail.isEmpty()) {
    if (!emptyThumbnail_.isNull()) {
      thumbnail_->setPixmap(emptyThumbnail_);
    } else {
      thumbnail_->clear();
    }
    thumbnailBytes_.clear();
    return;
  }
  if (!thumbnailBytes_.isEmpty() && thumbnailBytes_ == session->thumbnail) return;
  const QImage image = QImage::fromData(session->thumbnail);
  if (image.isNull()) {
    qWarning() << "Error creating media thumbnail";
    thumbnailBytes_.clear();
    thumbnail_->clear();
    return;
  }
  thumbnailBytes_ = session->thumbnail;
  thumbnail_->setPixmap(renderThumbnail(image));
}

QPixmap MediaSection::renderThumbnail(const QImage& source) const {
  const int side = std::min(source.width(), source.height());
  const QImage square =
      source.copy((source.width() - side) / 2, (source.height() - side) / 2, side, side);
  return roundedThumbnail(square);
}

// Scales a square image to thumbnail_size device pixels and rounds its corners.
QPixmap MediaSection::roundedThumbnail(const QImage& square) const {
  const int size = std::max(1, static_cast<int>(std::lround(options_.thumbnail_size * thumbDpr_)));
  const QImage resized = square.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                             .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QPixmap pixmap;
  if (options_.thumbnail_radius <= 0) {
    pixmap = QPixmap::fromImage(resized);
  } else {
    pixmap = QPixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    const double radius = options_.thumbnail_radius * thumbDpr_;
    path.addRoundedRect(0, 0, size, size, radius, radius);
    painter.setClipPath(path);
    painter.drawImage(0, 0, resized);
    painter.end();
  }
  pixmap.setDevicePixelRatio(thumbDpr_);
  return pixmap;
}

void MediaSection::updateControls() {
  platform::MediaSessionService* media = context_.system.media;
  if (const auto* session = media ? media->currentSession() : nullptr) updateControlsFrom(*session);
}

void MediaSection::updateControlsFrom(const platform::MediaSessionState& session) {
  const bool playing = session.isPlaying;
  if (!currentPlaying_ || playing != *currentPlaying_) {
    currentPlaying_ = playing;
    play_->setText(qs(playing ? options_.icons.pause : options_.icons.play));
  }
  if (session.playbackReady) {
    prev_->setEnabled(session.prevEnabled);
    play_->setEnabled(session.playEnabled);
    next_->setEnabled(session.nextEnabled);
  } else {
    prev_->setEnabled(true);
    play_->setEnabled(true);
    next_->setEnabled(true);
  }
  for (QLabel* button : {prev_, play_, next_}) {
    QStringList classes =
        button->property("class").toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    classes.removeAll(QStringLiteral("disabled"));
    if (!button->isEnabled()) classes << QStringLiteral("disabled");
    setCssClass(button, classes.join(QLatin1Char(' ')));
  }
}

void MediaSection::wheelEvent(QWheelEvent* event) {
  const int delta = event->angleDelta().y();
  if (delta == 0) return;
  if (context_.system.media) context_.system.media->switchCurrentSession(delta > 0 ? 1 : -1);
  event->accept();
}

}  // namespace yasb::widgets

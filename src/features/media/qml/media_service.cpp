#include "features/media/qml/media_service.h"

#include "kernel/qt/live_desktop.h"

namespace wabar::media {

QString imageMimeType(const QByteArray& bytes) {
  if (bytes.startsWith("\x89PNG\r\n\x1a\n")) return QStringLiteral("image/png");
  if (bytes.startsWith("\xff\xd8\xff")) return QStringLiteral("image/jpeg");
  if (bytes.startsWith("GIF8")) return QStringLiteral("image/gif");
  if (bytes.startsWith("BM")) return QStringLiteral("image/bmp");
  if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") {
    return QStringLiteral("image/webp");
  }
  return QStringLiteral("application/octet-stream");
}

QString imageDataUrl(const QByteArray& bytes) {
  if (bytes.isEmpty()) return {};
  return QStringLiteral("data:%1;base64,%2")
      .arg(imageMimeType(bytes), QString::fromLatin1(bytes.toBase64()));
}

namespace {

// GSMTC, its worker thread and the source-app resolver read the real desktop; a desktop that is
// not live (tests, the offscreen platform) never starts them, and `Media.available` stays false.
std::unique_ptr<MediaSessionSource> systemSource() {
  if (!kernel::liveDesktop(QStringLiteral("media"))) return nullptr;
  return createSystemMediaSource();
}

}  // namespace

std::shared_ptr<MediaSessionService> acquireMediaService() {
  static std::weak_ptr<MediaSessionService> shared;
  if (auto existing = shared.lock()) return existing;
  auto created = std::make_shared<MediaSessionService>(systemSource());
  shared = created;
  return created;
}

Media::Media(QObject* parent) : Media(acquireMediaService(), parent) {}

Media::Media(std::unique_ptr<MediaSessionSource> source, QObject* parent)
    : Media(std::make_shared<MediaSessionService>(std::move(source)), parent) {}

Media::Media(std::shared_ptr<MediaSessionService> service, QObject* parent)
    : QObject(parent), service_(std::move(service)) {
  for (auto signal :
       {&MediaSessionService::mediaDataChanged, &MediaSessionService::currentSessionChanged,
        &MediaSessionService::mediaPropertiesChanged, &MediaSessionService::timelineInfoChanged,
        &MediaSessionService::playbackInfoChanged}) {
    connect(service_.get(), signal, this, &Media::onServiceChanged);
  }
  connect(service_.get(), &MediaSessionService::positionTicked, this, &Media::positionChanged);
  service_->start();
}

// The service stops its source when its last holder lets go.
Media::~Media() = default;

void Media::onServiceChanged() {
  const QByteArray bytes = current() ? current()->thumbnail : QByteArray();
  if (bytes != thumbnailBytes_) {
    thumbnailBytes_ = bytes;
    thumbnailUrl_ = imageDataUrl(bytes);
  }
  emit changed();
  emit positionChanged();
}

QString Media::title() const { return current() ? current()->title : QString(); }
QString Media::artist() const { return current() ? current()->artist : QString(); }
QString Media::album() const { return current() ? current()->albumTitle : QString(); }
QString Media::sourceName() const { return current() ? current()->sourceName : QString(); }
bool Media::playing() const { return current() && current()->isPlaying; }
double Media::position() const { return current() ? current()->currentPos : 0; }
double Media::duration() const { return current() ? current()->duration : 0; }
bool Media::canPrevious() const { return current() && current()->prevEnabled; }
bool Media::canPlayPause() const { return current() && current()->playEnabled; }
bool Media::canNext() const { return current() && current()->nextEnabled; }

}  // namespace wabar::media

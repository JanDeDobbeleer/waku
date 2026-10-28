#include "spike/services/media_service.h"

namespace barnacle::services {

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

Media::Media(QObject* parent) : Media(platform::createSystemMediaSource(), parent) {}

Media::Media(std::unique_ptr<platform::MediaSessionSource> source, QObject* parent)
    : QObject(parent), service_(std::move(source), this) {
  using platform::MediaSessionService;
  for (auto signal :
       {&MediaSessionService::mediaDataChanged, &MediaSessionService::currentSessionChanged,
        &MediaSessionService::mediaPropertiesChanged, &MediaSessionService::timelineInfoChanged,
        &MediaSessionService::playbackInfoChanged}) {
    connect(&service_, signal, this, &Media::onServiceChanged);
  }
  service_.start();
}

Media::~Media() { service_.stop(); }

void Media::onServiceChanged() {
  const QByteArray bytes = current() ? current()->thumbnail : QByteArray();
  if (bytes != thumbnailBytes_) {
    thumbnailBytes_ = bytes;
    thumbnailUrl_ = imageDataUrl(bytes);
  }
  emit changed();
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

}  // namespace barnacle::services

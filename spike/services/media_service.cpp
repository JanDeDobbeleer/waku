#include "spike/services/media_service.h"

namespace yasb::services {

Media::Media(QObject* parent)
    : QObject(parent), service_(platform::createSystemMediaSource(), this) {
  using platform::MediaSessionService;
  for (auto signal :
       {&MediaSessionService::mediaDataChanged, &MediaSessionService::currentSessionChanged,
        &MediaSessionService::mediaPropertiesChanged, &MediaSessionService::timelineInfoChanged,
        &MediaSessionService::playbackInfoChanged}) {
    connect(&service_, signal, this, &Media::changed);
  }
  service_.start();
}

Media::~Media() { service_.stop(); }

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

}  // namespace yasb::services

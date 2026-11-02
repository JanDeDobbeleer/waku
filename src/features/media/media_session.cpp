#include "features/media/media_session.h"

#include <QDateTime>
#include <QPointer>
#include <algorithm>
#include <utility>

namespace waku::media {

// Re-posts every source event to the service's thread. Events for a destroyed service are dropped
// by Qt together with the service's posted events.
class MediaSessionService::Sink final : public MediaSessionSink {
 public:
  explicit Sink(MediaSessionService* service) : service_(service) {}

  void sessionsChanged(std::vector<MediaSessionData> sessions, QString systemCurrentId,
                       bool followSystem) override {
    post([sessions = std::move(sessions), systemCurrentId = std::move(systemCurrentId),
          followSystem](MediaSessionService& s) {
      s.applySessions(sessions, systemCurrentId, followSystem);
    });
  }
  void propertiesChanged(QString appId, MediaProperties properties) override {
    post([appId = std::move(appId), properties = std::move(properties)](MediaSessionService& s) {
      s.applyProperties(appId, properties);
    });
  }
  void timelineChanged(QString appId, MediaTimeline timeline) override {
    post([appId = std::move(appId), timeline](MediaSessionService& s) {
      s.applyTimeline(appId, timeline);
    });
  }
  void playbackChanged(QString appId, MediaPlayback playback) override {
    post([appId = std::move(appId), playback](MediaSessionService& s) {
      s.applyPlayback(appId, playback);
    });
  }

 private:
  template <typename Fn>
  void post(Fn&& fn) {
    MediaSessionService* service = service_;
    QMetaObject::invokeMethod(
        service,
        [service, fn = std::forward<Fn>(fn)]() mutable {
          try {
            fn(*service);
          } catch (const std::exception& error) {
            qWarning().noquote() << "Media session update failed:" << error.what();
          }
        },
        Qt::QueuedConnection);
  }

  MediaSessionService* service_;
};

MediaSessionService::MediaSessionService(std::unique_ptr<MediaSessionSource> source,
                                         QObject* parent, Clock clock)
    : QObject(parent),
      source_(std::move(source)),
      sink_(std::make_unique<Sink>(this)),
      clock_(clock ? std::move(clock)
                   : Clock([] { return QDateTime::currentMSecsSinceEpoch() / 1000.0; })) {
  timer_.setInterval(kRefreshIntervalMs);
  connect(&timer_, &QTimer::timeout, this, [this] {
    interpolate();
    emit positionTicked();
  });
}

MediaSessionService::~MediaSessionService() { stop(); }

void MediaSessionService::start() {
  if (started_ || !source_) return;
  started_ = true;
  source_->start(sink_.get());
}

void MediaSessionService::stop() {
  timer_.stop();
  if (started_ && source_) source_->stop();
  started_ = false;
}

const MediaSessionState* MediaSessionService::currentSession() const {
  for (const auto& state : sessions_) {
    if (state.appId == currentId_) return &state;
  }
  return nullptr;
}

MediaSessionState* MediaSessionService::find(const QString& appId) {
  for (auto& state : sessions_) {
    if (state.appId == appId) return &state;
  }
  return nullptr;
}

void MediaSessionService::applySessions(const std::vector<MediaSessionData>& sessions,
                                        const QString& systemCurrentId, bool followSystem) {
  bool propertiesChanged = false;
  bool timelineChanged = false;
  bool playbackChanged = false;
  std::erase_if(sessions_, [&](const MediaSessionState& state) {
    return std::none_of(sessions.begin(), sessions.end(),
                        [&](const MediaSessionData& data) { return data.appId == state.appId; });
  });
  for (const MediaSessionData& data : sessions) {
    if (MediaSessionState* existing = find(data.appId)) {
      if (!data.sourceName.isEmpty()) existing->sourceName = data.sourceName;
    } else {
      MediaSessionState state;
      state.appId = data.appId;
      state.sourceName = data.sourceName;
      sessions_.push_back(std::move(state));
    }
    if (data.properties) {
      if (MediaSessionState* state = find(data.appId)) {
        state->title = data.properties->title;
        state->artist = data.properties->artist;
        state->albumTitle = data.properties->albumTitle;
        state->thumbnail = data.properties->thumbnail;
        propertiesChanged = true;
      }
    }
    if (data.timeline) {
      if (MediaSessionState* state = find(data.appId)) {
        if (applyTimelineSnapshot(*state, *data.timeline)) timelineChanged = true;
      }
    }
    if (data.playback) {
      if (MediaSessionState* state = find(data.appId)) {
        applyPlaybackSnapshot(*state, *data.playback);
        playbackChanged = true;
      }
    }
  }
  // Each kind once for the whole list, not once per session: every emission re-syncs every
  // MprisPlayer and re-evaluates every Media binding.
  if (propertiesChanged) emit mediaPropertiesChanged();
  if (timelineChanged) emit timelineInfoChanged();
  if (playbackChanged) emit playbackInfoChanged();
  const bool selectionChanged = applyCurrentSelection(systemCurrentId, followSystem);
  emit mediaDataChanged();
  if (followSystem) {
    emit currentSessionChanged();
  } else if (selectionChanged) {
    emit mediaPropertiesChanged();
    if (const MediaSessionState* current = currentSession(); current && current->playbackReady)
      emit playbackInfoChanged();
    emit currentSessionChanged();
  }
  // Per-session timeline events above fire before the selection is known; tell listeners again so
  // flyout labels follow a session switch.
  if (const MediaSessionState* current = currentSession(); current && current->lastUpdateTime > 0)
    emit timelineInfoChanged();
  updateTimer();
  interpolateAndEmit();
}

void MediaSessionService::applyProperties(const QString& appId, const MediaProperties& properties) {
  MediaSessionState* state = find(appId);
  if (!state) return;
  state->title = properties.title;
  state->artist = properties.artist;
  state->albumTitle = properties.albumTitle;
  state->thumbnail = properties.thumbnail;
  emit mediaPropertiesChanged();
  interpolateAndEmit();
}

void MediaSessionService::applyTimeline(const QString& appId, const MediaTimeline& timeline) {
  MediaSessionState* state = find(appId);
  if (!state) return;
  if (applyTimelineSnapshot(*state, timeline)) emit timelineInfoChanged();
  updateTimer();
  interpolateAndEmit();
}

void MediaSessionService::applyPlayback(const QString& appId, const MediaPlayback& playback) {
  MediaSessionState* state = find(appId);
  if (!state) return;
  applyPlaybackSnapshot(*state, playback);
  emit playbackInfoChanged();
  updateTimer();
  interpolateAndEmit();
}

bool MediaSessionService::applyTimelineSnapshot(MediaSessionState& state,
                                                const MediaTimeline& timeline) {
  // An unset WinRT DateTime (1601) would make the interpolated position explode.
  if (timeline.lastUpdated <= 0) return false;
  state.duration = timeline.duration;
  if (timeline.lastUpdated > state.lastUpdateTime) state.lastSnapshotPos = timeline.position;
  state.lastUpdateTime = timeline.lastUpdated;
  return true;
}

void MediaSessionService::applyPlaybackSnapshot(MediaSessionState& state,
                                                const MediaPlayback& playback) {
  state.playbackStatus = playback.status;
  state.isPlaying = playback.status == 4;
  state.playbackRate = playback.rate > 0 ? playback.rate : 1.0;
  state.prevEnabled = playback.prevEnabled;
  state.playEnabled = playback.playEnabled;
  state.nextEnabled = playback.nextEnabled;
  state.timelineEnabled = playback.timelineEnabled;
  state.shuffleEnabled = playback.shuffleEnabled;
  state.repeatEnabled = playback.repeatEnabled;
  state.shuffleActive = playback.shuffleActive;
  state.autoRepeatMode = playback.autoRepeatMode;
  state.playbackReady = true;
}

bool MediaSessionService::applyCurrentSelection(const QString& systemId, bool followSystem) {
  const QString previous = currentId_;
  if (followSystem || !find(currentId_)) {
    if (!systemId.isEmpty() && find(systemId)) {
      currentId_ = systemId;
    } else if (!sessions_.empty()) {
      currentId_ = sessions_.front().appId;
    } else {
      currentId_.clear();
    }
  }
  for (auto& state : sessions_)
    state.isCurrent = !currentId_.isEmpty() && state.appId == currentId_;
  return previous != currentId_;
}

void MediaSessionService::interpolateAndEmit() {
  interpolate();
  emit mediaDataChanged();
}

void MediaSessionService::interpolate() {
  const double now = clock_();
  for (auto& state : sessions_) {
    double pos = state.lastSnapshotPos;
    if (state.isPlaying && state.lastUpdateTime > 0)
      pos += (now - state.lastUpdateTime) * state.playbackRate;
    state.currentPos = pos;
  }
}

void MediaSessionService::updateTimer() {
  const MediaSessionState* current = currentSession();
  const bool playing = current && current->isPlaying && current->lastUpdateTime > 0;
  if (playing && !timer_.isActive()) {
    timer_.start();
  } else if (!playing && timer_.isActive()) {
    timer_.stop();
  }
}

void MediaSessionService::send(MediaCommand command, double value) {
  const MediaSessionState* current = currentSession();
  if (!current || !source_) return;
  source_->command(current->appId, command, value);
}

void MediaSessionService::playPause() { send(MediaCommand::PlayPause); }

void MediaSessionService::next() { send(MediaCommand::Next); }

void MediaSessionService::previous() { send(MediaCommand::Previous); }

void MediaSessionService::seek(double seconds) {
  MediaSessionState* current = find(currentId_);
  if (!current) return;
  send(MediaCommand::Seek, seconds);
  current->lastSnapshotPos = seconds;
  current->lastUpdateTime = clock_();
  interpolateAndEmit();
}

void MediaSessionService::toggleShuffle() {
  const MediaSessionState* current = currentSession();
  if (!current || !current->shuffleEnabled) return;
  send(MediaCommand::SetShuffle, current->shuffleActive ? 0 : 1);
}

void MediaSessionService::cycleRepeat() {
  const MediaSessionState* current = currentSession();
  if (!current || !current->repeatEnabled) return;
  // None -> List -> Track -> None
  const int next = current->autoRepeatMode == 0 ? 2 : current->autoRepeatMode == 2 ? 1 : 0;
  send(MediaCommand::SetRepeat, next);
}

void MediaSessionService::forceUpdate() {
  emit mediaPropertiesChanged();
  interpolateAndEmit();
}

void MediaSessionService::switchCurrentSession(int direction) {
  if (sessions_.empty()) return;
  const int count = static_cast<int>(sessions_.size());
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (sessions_[static_cast<size_t>(i)].appId == currentId_) {
      index = i;
      break;
    }
  }
  const int target = ((index + direction) % count + count) % count;
  MediaSessionState& next = sessions_[static_cast<size_t>(target)];
  if (next.appId == currentId_) return;
  for (auto& state : sessions_) state.isCurrent = false;
  next.isCurrent = true;
  currentId_ = next.appId;
  const bool ready = next.playbackReady;
  emit mediaDataChanged();
  emit mediaPropertiesChanged();
  if (ready) emit playbackInfoChanged();
  emit currentSessionChanged();
  updateTimer();
  interpolateAndEmit();
}

const MediaSessionState* MediaSessionService::findSession(const QString& appId) const {
  for (const auto& state : sessions_) {
    if (state.appId == appId) return &state;
  }
  return nullptr;
}

void MediaSessionService::sendTo(const QString& appId, MediaCommand command, double value) {
  if (!source_ || !findSession(appId)) return;
  source_->command(appId, command, value);
}

void MediaSessionService::seekSession(const QString& appId, double seconds) {
  MediaSessionState* state = find(appId);
  if (!state || !source_) return;
  source_->command(appId, MediaCommand::Seek, seconds);
  state->lastSnapshotPos = seconds;
  state->lastUpdateTime = clock_();
  interpolateAndEmit();
}

void MediaSessionService::selectSession(const QString& appId) {
  MediaSessionState* next = find(appId);
  if (!next || appId == currentId_) return;
  for (auto& state : sessions_) state.isCurrent = false;
  next->isCurrent = true;
  currentId_ = appId;
  const bool ready = next->playbackReady;
  emit mediaDataChanged();
  emit mediaPropertiesChanged();
  if (ready) emit playbackInfoChanged();
  emit currentSessionChanged();
  updateTimer();
  interpolateAndEmit();
}

}  // namespace waku::media

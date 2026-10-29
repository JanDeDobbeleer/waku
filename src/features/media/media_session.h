#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace wabar::media {

// Plain snapshots read from GlobalSystemMediaTransportControls (no COM objects are retained).
struct MediaProperties {
  QString title;
  QString artist;
  QString albumTitle;
  QByteArray thumbnail;  // encoded image bytes as delivered by the source app; empty if none
};

struct MediaTimeline {
  double position = 0;     // seconds
  double lastUpdated = 0;  // unix seconds; <= 0 means unset (WinRT 1601 epoch)
  double duration = 0;     // seconds (EndTime)
};

struct MediaPlayback {
  int status = 0;  // 0 Closed, 1 Opened, 2 Changing, 3 Stopped, 4 Playing, 5 Paused
  double rate = 1.0;
  bool prevEnabled = true;
  bool playEnabled = true;
  bool nextEnabled = true;
  bool timelineEnabled = false;
  bool shuffleEnabled = false;
  bool repeatEnabled = false;
  bool shuffleActive = false;
  int autoRepeatMode = 0;  // 0 None, 1 Track, 2 List
};

// One session as read by a source. Missing parts are ones the source could not read.
struct MediaSessionData {
  QString appId;
  QString sourceName;  // resolved display name of the source app ("" when unknown)
  std::optional<MediaProperties> properties;
  std::optional<MediaTimeline> timeline;
  std::optional<MediaPlayback> playback;
};

// GUI-thread session state (Python SessionState).
struct MediaSessionState {
  QString appId;
  QString sourceName;
  QString title;
  QString artist;
  QString albumTitle;
  QByteArray thumbnail;
  double lastSnapshotPos = 0;
  double lastUpdateTime = 0;
  double duration = 0;
  double currentPos = 0;
  bool isPlaying = false;
  int playbackStatus = 0;
  double playbackRate = 1.0;
  bool isCurrent = false;
  bool timelineEnabled = false;
  bool playbackReady = false;
  bool prevEnabled = true;
  bool playEnabled = true;
  bool nextEnabled = true;
  bool shuffleEnabled = false;
  bool repeatEnabled = false;
  bool shuffleActive = false;
  int autoRepeatMode = 0;
};

enum class MediaCommand { PlayPause, Next, Previous, Seek, SetShuffle, SetRepeat };

// Receives source events. A source may call it from any thread.
class MediaSessionSink {
 public:
  virtual ~MediaSessionSink() = default;
  // Full session list (in system order); `followSystem` mirrors Python's current_session_changed
  // path, otherwise sessions_changed.
  virtual void sessionsChanged(std::vector<MediaSessionData> sessions, QString systemCurrentId,
                               bool followSystem) = 0;
  virtual void propertiesChanged(QString appId, MediaProperties properties) = 0;
  virtual void timelineChanged(QString appId, MediaTimeline timeline) = 0;
  virtual void playbackChanged(QString appId, MediaPlayback playback) = 0;
};

class MediaSessionSource {
 public:
  virtual ~MediaSessionSource() = default;
  // `sink` must stay valid until stop() returns; no sink call happens after that.
  virtual void start(MediaSessionSink* sink) = 0;
  virtual void stop() = 0;
  // `value`: seconds for Seek, 0/1 for SetShuffle, repeat mode for SetRepeat.
  virtual void command(const QString& appId, MediaCommand command, double value = 0) = 0;
};

// GlobalSystemMediaTransportControlsSessionManager on a dedicated worker thread.
std::unique_ptr<MediaSessionSource> createSystemMediaSource();

// Shared media session service (port of services/media/media.py WindowsMedia), reused by the
// media widget and the control center through the runtime's shared-service registry.
//
// Threading: the object lives on the GUI thread and owns its source. The source's worker thread
// only calls the sink, which re-posts every event to this object's thread; all state below is
// GUI-thread only. stop() (or the destructor) stops the source and joins its thread.
// Position interpolation runs on a GUI-thread QTimer that is active only while a session plays.
class MediaSessionService final : public QObject {
  Q_OBJECT

 public:
  using Clock = std::function<double()>;  // unix seconds
  static constexpr int kRefreshIntervalMs = 100;
  static constexpr const char* kServiceKey = "media";

  explicit MediaSessionService(std::unique_ptr<MediaSessionSource> source,
                               QObject* parent = nullptr, Clock clock = {});
  ~MediaSessionService() override;

  void start();
  Q_INVOKABLE void stop();

  const std::vector<MediaSessionState>& sessions() const { return sessions_; }
  const MediaSessionState* currentSession() const;
  bool isInterpolating() const { return timer_.isActive(); }

  void playPause();
  void next();
  void previous();
  void seek(double seconds);
  void toggleShuffle();
  void cycleRepeat();
  void forceUpdate();
  void switchCurrentSession(int direction);

  // Apply source events on the GUI thread (the sink posts here).
  void applySessions(const std::vector<MediaSessionData>& sessions, const QString& systemCurrentId,
                     bool followSystem);
  void applyProperties(const QString& appId, const MediaProperties& properties);
  void applyTimeline(const QString& appId, const MediaTimeline& timeline);
  void applyPlayback(const QString& appId, const MediaPlayback& playback);

 signals:
  void mediaDataChanged();
  void currentSessionChanged();
  void mediaPropertiesChanged();
  void timelineInfoChanged();
  void playbackInfoChanged();

 private:
  class Sink;
  MediaSessionState* find(const QString& appId);
  bool applyTimelineSnapshot(MediaSessionState& state, const MediaTimeline& timeline);
  void applyPlaybackSnapshot(MediaSessionState& state, const MediaPlayback& playback);
  bool applyCurrentSelection(const QString& systemId, bool followSystem);
  void interpolateAndEmit();
  void updateTimer();
  void send(MediaCommand command, double value = 0);

  std::unique_ptr<MediaSessionSource> source_;
  std::unique_ptr<Sink> sink_;
  Clock clock_;
  std::vector<MediaSessionState> sessions_;
  QString currentId_;
  QTimer timer_;
  bool started_ = false;
};

// Display name for a media source AUMID (source_apps.resolve_source_app_name). Cached; may block
// briefly, call it off the GUI thread.
QString resolveSourceAppName(const QString& aumid);
// source_apps.get_source_app_class_name.
QString sourceAppClassName(const QString& displayName);

}  // namespace wabar::media

// GlobalSystemMediaTransportControlsSessionManager source for MediaSessionService.
//
// Threading: one dedicated worker thread (kernel::TaskThread, MTA) owns every WinRT object. WinRT
// event handlers run on thread-pool threads and only enqueue a task (an app id and what to re-read)
// on the worker's queue; the worker reads plain snapshots and hands them to the sink. command()
// enqueues too, so the GUI thread never touches WinRT. The thread sleeps between events; there is
// no polling. stop() revokes the handlers, wakes the worker and joins it; a refresh in
// progress stops at the next session, so the join waits for one WinRT read at most. start() after
// stop() starts a fresh worker.
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <QDebug>
#include <QStringList>
#include <map>
#include <optional>
#include <vector>

#include "features/media/media_session.h"
#include "kernel/win32/task_thread.h"
#include "kernel/win32/winrt_strings.h"

namespace wabar::media {
namespace {

namespace wmc = winrt::Windows::Media::Control;
using wmc::GlobalSystemMediaTransportControlsSession;
using wmc::GlobalSystemMediaTransportControlsSessionManager;

constexpr double kTicksPerSecond = 10000000.0;
constexpr double kUnixEpochOffsetSeconds = 11644473600.0;  // 1601-01-01 to 1970-01-01

using kernel::toQString;

double seconds(winrt::Windows::Foundation::TimeSpan span) {
  return static_cast<double>(span.count()) / kTicksPerSecond;
}

double unixSeconds(winrt::Windows::Foundation::DateTime time) {
  return static_cast<double>(time.time_since_epoch().count()) / kTicksPerSecond -
         kUnixEpochOffsetSeconds;
}

class SystemMediaSource final : public MediaSessionSource {
 public:
  ~SystemMediaSource() override { stop(); }

  void start(MediaSessionSink* sink) override {
    if (worker_.running()) return;
    sink_ = sink;
    worker_.start([this] { setUp(); }, [this] { tearDown(); });
  }

  void stop() override {
    if (!worker_.running()) return;
    worker_.stop();
    sink_ = nullptr;
  }

  void command(const QString& appId, MediaCommand command, double value) override {
    // A command while stopped is dropped, not kept for the next start().
    if (!worker_.running()) return;
    worker_.post([this, appId, command, value] { execute(appId, command, value); });
  }

 private:
  struct Tracked {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    winrt::event_token propertiesToken;
    winrt::event_token timelineToken;
    winrt::event_token playbackToken;
  };

  // Worker thread, first: the manager, its handlers and one full read.
  void setUp() {
    try {
      manager_ = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
      const kernel::TaskThread::Poster post = worker_.poster();
      sessionsToken_ = manager_.SessionsChanged(
          [post, this](auto&&, auto&&) { post([this] { refresh(false); }); });
      currentToken_ = manager_.CurrentSessionChanged(
          [post, this](auto&&, auto&&) { post([this] { refresh(true); }); });
      // One full read; refresh(false) first would read every session and its thumbnail twice.
      refresh(true);
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Media session manager unavailable:" << toQString(error.message());
    } catch (const std::exception& error) {
      qWarning().noquote() << "Media session manager unavailable:" << error.what();
    }
  }

  // Worker thread, last: every handler revoked, every WinRT object released.
  void tearDown() {
    try {
      if (manager_) {
        manager_.SessionsChanged(sessionsToken_);
        manager_.CurrentSessionChanged(currentToken_);
      }
      for (auto& [id, tracked] : tracked_) unbind(tracked);
    } catch (...) {
    }
    tracked_.clear();
    manager_ = nullptr;
  }

  // The handlers hold a Poster, not the source, so a late event after the source is destroyed
  // only reaches a queue nobody reads.
  void bind(const QString& appId, Tracked& tracked) {
    const kernel::TaskThread::Poster post = worker_.poster();
    tracked.propertiesToken = tracked.session.MediaPropertiesChanged(
        [post, this, appId](auto&&, auto&&) { post([this, appId] { publishProperties(appId); }); });
    tracked.timelineToken = tracked.session.TimelinePropertiesChanged(
        [post, this, appId](auto&&, auto&&) { post([this, appId] { publishTimeline(appId); }); });
    tracked.playbackToken = tracked.session.PlaybackInfoChanged(
        [post, this, appId](auto&&, auto&&) { post([this, appId] { publishPlayback(appId); }); });
  }

  // Never throws: revoking on a session whose app has gone can fail, and that must not abort the
  // refresh that drops it.
  static void unbind(Tracked& tracked) noexcept {
    if (!tracked.session) return;
    try {
      tracked.session.MediaPropertiesChanged(tracked.propertiesToken);
      tracked.session.TimelinePropertiesChanged(tracked.timelineToken);
      tracked.session.PlaybackInfoChanged(tracked.playbackToken);
    } catch (...) {
    }
  }

  static std::optional<MediaProperties> readProperties(
      const GlobalSystemMediaTransportControlsSession& session) {
    try {
      const auto info = session.TryGetMediaPropertiesAsync().get();
      if (!info) return std::nullopt;
      MediaProperties result;
      result.title = toQString(info.Title());
      result.artist = toQString(info.Artist());
      result.albumTitle = toQString(info.AlbumTitle());
      if (const auto reference = info.Thumbnail()) {
        try {
          const auto stream = reference.OpenReadAsync().get();
          const auto size = static_cast<uint32_t>(stream.Size());
          if (size > 0) {
            winrt::Windows::Storage::Streams::DataReader reader(stream);
            reader.LoadAsync(size).get();
            QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
            reader.ReadBytes(
                winrt::array_view<uint8_t>(reinterpret_cast<uint8_t*>(bytes.data()),
                                           reinterpret_cast<uint8_t*>(bytes.data()) + size));
            result.thumbnail = std::move(bytes);
            reader.Close();
          }
          stream.Close();
        } catch (const winrt::hresult_error&) {
          // No thumbnail; the widget draws its default.
        }
      }
      return result;
    } catch (const winrt::hresult_error&) {
      return std::nullopt;
    }
  }

  static std::optional<MediaTimeline> readTimeline(
      const GlobalSystemMediaTransportControlsSession& session) {
    try {
      const auto timeline = session.GetTimelineProperties();
      if (!timeline) return std::nullopt;
      MediaTimeline result;
      result.position = seconds(timeline.Position());
      result.lastUpdated = unixSeconds(timeline.LastUpdatedTime());
      result.duration = seconds(timeline.EndTime());
      return result;
    } catch (const winrt::hresult_error&) {
      return std::nullopt;
    }
  }

  static std::optional<MediaPlayback> readPlayback(
      const GlobalSystemMediaTransportControlsSession& session) {
    try {
      const auto info = session.GetPlaybackInfo();
      if (!info) return std::nullopt;
      MediaPlayback result;
      result.status = static_cast<int>(info.PlaybackStatus());
      if (const auto rate = info.PlaybackRate()) {
        const double value = rate.Value();
        if (value > 0) result.rate = value;
      }
      if (const auto controls = info.Controls()) {
        result.prevEnabled = controls.IsPreviousEnabled();
        result.playEnabled = controls.IsPlayPauseToggleEnabled();
        result.nextEnabled = controls.IsNextEnabled();
        result.timelineEnabled = controls.IsPlaybackPositionEnabled();
        result.shuffleEnabled = controls.IsShuffleEnabled();
        result.repeatEnabled = controls.IsRepeatEnabled();
      } else {
        result.timelineEnabled = false;
        result.shuffleEnabled = false;
        result.repeatEnabled = false;
      }
      if (const auto shuffle = info.IsShuffleActive()) result.shuffleActive = shuffle.Value();
      if (const auto repeat = info.AutoRepeatMode())
        result.autoRepeatMode = static_cast<int>(repeat.Value());
      return result;
    } catch (const winrt::hresult_error&) {
      return std::nullopt;
    }
  }

  // Python _refresh_sessions: rebuild trackers, then read every session in full.
  void refresh(bool followSystem) {
    if (!manager_ || !sink_) return;
    std::vector<MediaSessionData> data;
    QString currentId;
    std::map<QString, GlobalSystemMediaTransportControlsSession> live;
    QStringList order;
    for (const auto& session : manager_.GetSessions()) {
      const QString appId = toQString(session.SourceAppUserModelId());
      if (live.count(appId) != 0) continue;
      live.emplace(appId, session);
      order.push_back(appId);
    }
    if (const auto current = manager_.GetCurrentSession())
      currentId = toQString(current.SourceAppUserModelId());

    for (auto it = tracked_.begin(); it != tracked_.end();) {
      if (live.count(it->first) == 0) {
        unbind(it->second);
        it = tracked_.erase(it);
      } else {
        ++it;
      }
    }
    for (const QString& appId : order) {
      // stop() does not wait for every session of a refresh (each read can block on a frozen
      // player).
      if (worker_.stopRequested()) return;
      if (tracked_.count(appId) == 0) {
        Tracked tracked;
        tracked.session = live.at(appId);
        bind(appId, tracked);
        tracked_.emplace(appId, std::move(tracked));
      }
      const auto& session = tracked_.at(appId).session;
      MediaSessionData entry;
      entry.appId = appId;
      entry.sourceName = resolveSourceAppName(appId);
      entry.properties = readProperties(session);
      entry.timeline = readTimeline(session);
      entry.playback = readPlayback(session);
      data.push_back(std::move(entry));
    }
    sink_->sessionsChanged(std::move(data), currentId, followSystem);
  }

  const GlobalSystemMediaTransportControlsSession* session(const QString& appId) const {
    const auto it = tracked_.find(appId);
    return it == tracked_.end() ? nullptr : &it->second.session;
  }

  void publishProperties(const QString& appId) {
    const auto* s = session(appId);
    if (!s || !sink_) return;
    if (auto properties = readProperties(*s))
      sink_->propertiesChanged(appId, std::move(*properties));
  }

  void publishTimeline(const QString& appId) {
    const auto* s = session(appId);
    if (!s || !sink_) return;
    if (auto timeline = readTimeline(*s)) sink_->timelineChanged(appId, *timeline);
  }

  void publishPlayback(const QString& appId) {
    const auto* s = session(appId);
    if (!s || !sink_) return;
    if (auto playback = readPlayback(*s)) sink_->playbackChanged(appId, *playback);
  }

  void execute(const QString& appId, MediaCommand command, double value) {
    const auto* s = session(appId);
    if (!s) return;
    try {
      switch (command) {
        case MediaCommand::PlayPause:
          s->TryTogglePlayPauseAsync();
          break;
        case MediaCommand::Next:
          s->TrySkipNextAsync();
          break;
        case MediaCommand::Previous:
          s->TrySkipPreviousAsync();
          break;
        case MediaCommand::Play:
          s->TryPlayAsync();
          break;
        case MediaCommand::Pause:
          s->TryPauseAsync();
          break;
        case MediaCommand::Stop:
          s->TryStopAsync();
          break;
        case MediaCommand::Seek:
          s->TryChangePlaybackPositionAsync(static_cast<int64_t>(value * kTicksPerSecond));
          break;
        case MediaCommand::SetShuffle:
          s->TryChangeShuffleActiveAsync(value != 0);
          break;
        case MediaCommand::SetRepeat:
          s->TryChangeAutoRepeatModeAsync(
              static_cast<winrt::Windows::Media::MediaPlaybackAutoRepeatMode>(
                  static_cast<int>(value)));
          break;
      }
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Media command failed:" << toQString(error.message());
    }
  }

  kernel::TaskThread worker_{QStringLiteral("Media session")};
  MediaSessionSink* sink_ = nullptr;  // set before the thread starts, cleared after it joins
  // Worker-thread only from here down.
  GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
  winrt::event_token sessionsToken_;
  winrt::event_token currentToken_;
  std::map<QString, Tracked> tracked_;
};

}  // namespace

std::unique_ptr<MediaSessionSource> createSystemMediaSource() {
  return std::make_unique<SystemMediaSource>();
}

}  // namespace wabar::media

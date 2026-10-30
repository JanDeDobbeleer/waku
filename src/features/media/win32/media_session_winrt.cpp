// GlobalSystemMediaTransportControlsSessionManager source for MediaSessionService.
//
// Threading: one dedicated worker thread (MTA) owns every WinRT object. WinRT event handlers run
// on thread-pool threads and only enqueue a task (an app id and what to re-read) on the worker's
// queue; the worker reads plain snapshots and hands them to the sink. command() enqueues too, so
// the GUI thread never touches WinRT. The thread sleeps on a condition variable between events;
// there is no polling. stop() revokes the handlers, wakes the worker and joins it.
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <QDebug>
#include <QStringList>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "features/media/media_session.h"

namespace wabar::media {
namespace {

namespace wmc = winrt::Windows::Media::Control;
using wmc::GlobalSystemMediaTransportControlsSession;
using wmc::GlobalSystemMediaTransportControlsSessionManager;

constexpr double kTicksPerSecond = 10000000.0;
constexpr double kUnixEpochOffsetSeconds = 11644473600.0;  // 1601-01-01 to 1970-01-01

QString toQString(const winrt::hstring& text) {
  return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

double seconds(winrt::Windows::Foundation::TimeSpan span) {
  return static_cast<double>(span.count()) / kTicksPerSecond;
}

double unixSeconds(winrt::Windows::Foundation::DateTime time) {
  return static_cast<double>(time.time_since_epoch().count()) / kTicksPerSecond -
         kUnixEpochOffsetSeconds;
}

// Task queue shared with WinRT handlers; handlers hold it by shared_ptr so a late event after the
// source is destroyed only touches a queue nobody reads.
struct Queue {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> tasks;
  bool stopping = false;

  void post(std::function<void()> task) {
    {
      std::lock_guard lock(mutex);
      if (stopping) return;
      tasks.push_back(std::move(task));
    }
    wake.notify_one();
  }
};

class SystemMediaSource final : public MediaSessionSource {
 public:
  ~SystemMediaSource() override { stop(); }

  void start(MediaSessionSink* sink) override {
    if (thread_.joinable()) return;
    sink_ = sink;
    thread_ = std::thread([this] { run(); });
  }

  void stop() override {
    if (!thread_.joinable()) return;
    {
      std::lock_guard lock(queue_->mutex);
      queue_->stopping = true;
    }
    queue_->wake.notify_all();
    thread_.join();
    sink_ = nullptr;
  }

  void command(const QString& appId, MediaCommand command, double value) override {
    queue_->post([this, appId, command, value] { execute(appId, command, value); });
  }

 private:
  struct Tracked {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    winrt::event_token propertiesToken;
    winrt::event_token timelineToken;
    winrt::event_token playbackToken;
  };

  void run() {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
      // Already initialised on this thread; carry on.
    }
    try {
      manager_ = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
      const std::shared_ptr<Queue> queue = queue_;
      sessionsToken_ = manager_.SessionsChanged(
          [queue, this](auto&&, auto&&) { queue->post([this] { refresh(false); }); });
      currentToken_ = manager_.CurrentSessionChanged(
          [queue, this](auto&&, auto&&) { queue->post([this] { refresh(true); }); });
      refresh(false);
      refresh(true);
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Media session manager unavailable:" << toQString(error.message());
    } catch (const std::exception& error) {
      qWarning().noquote() << "Media session manager unavailable:" << error.what();
    }

    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(queue_->mutex);
        queue_->wake.wait(lock, [this] { return queue_->stopping || !queue_->tasks.empty(); });
        if (queue_->stopping) break;
        task = std::move(queue_->tasks.front());
        queue_->tasks.pop_front();
      }
      try {
        task();
      } catch (const winrt::hresult_error& error) {
        qWarning().noquote() << "Media session task failed:" << toQString(error.message());
      } catch (const std::exception& error) {
        qWarning().noquote() << "Media session task failed:" << error.what();
      }
    }

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
    winrt::uninit_apartment();
  }

  void bind(const QString& appId, Tracked& tracked) {
    const std::shared_ptr<Queue> queue = queue_;
    tracked.propertiesToken =
        tracked.session.MediaPropertiesChanged([queue, this, appId](auto&&, auto&&) {
          queue->post([this, appId] { publishProperties(appId); });
        });
    tracked.timelineToken =
        tracked.session.TimelinePropertiesChanged([queue, this, appId](auto&&, auto&&) {
          queue->post([this, appId] { publishTimeline(appId); });
        });
    tracked.playbackToken =
        tracked.session.PlaybackInfoChanged([queue, this, appId](auto&&, auto&&) {
          queue->post([this, appId] { publishPlayback(appId); });
        });
  }

  static void unbind(Tracked& tracked) {
    if (!tracked.session) return;
    tracked.session.MediaPropertiesChanged(tracked.propertiesToken);
    tracked.session.TimelinePropertiesChanged(tracked.timelineToken);
    tracked.session.PlaybackInfoChanged(tracked.playbackToken);
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

  std::shared_ptr<Queue> queue_ = std::make_shared<Queue>();
  std::thread thread_;
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

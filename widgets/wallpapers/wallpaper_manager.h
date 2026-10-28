#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>
#include <random>

#include "platform/desktop_wallpaper.h"
#include "widgets/wallpapers/wallpaper_engine.h"

namespace yasb::widgets {

// Port of services/wallpapers/manager.py (WallpaperManager). One per runtime, shared by every
// wallpapers widget through the polling-worker registry's service map (key "wallpapers"): the
// random-change timer, the "a change is running" guard and the post-change commands.
//
// Thread: GUI thread only. The one background thread involved is the engine's image decoder, owned
// and joined by WallpaperEngine; post-change commands run as async QProcess children.
// stop() (called by the registry at shutdown) stops the timer and removes the overlay.
class WallpaperManager : public QObject {
  Q_OBJECT
 public:
  using BackendFactory = std::function<std::unique_ptr<platform::DesktopWallpaperBackend>()>;
  // Returns an unstarted engine for `image` / `animation` (the manager connects and starts it).
  using EngineFactory =
      std::function<WallpaperEngine*(const QString& image, const QString& animation)>;
  // Runs one post-change command line (cmd.exe semantics) and calls `done` with its stderr.
  using CommandRunner =
      std::function<void(const QString& command, std::function<void(const QString&)> done)>;
  // Index in [0, count) for the random wallpaper.
  using Picker = std::function<int(int count)>;

  struct Dependencies {
    BackendFactory backend;
    EngineFactory engine;
    CommandRunner runner;
    Picker picker;
  };

  struct EngineOptions {
    bool enabled = false;
    QString animation = QStringLiteral("circle");
  };

  explicit WallpaperManager(Dependencies deps = {}, QObject* parent = nullptr);
  ~WallpaperManager() override;

  void configure(const QStringList& imagePaths, int updateInterval, bool changeAutomatically,
                 const QStringList& runAfter, const EngineOptions& engine);

  // Sets the wallpaper on every monitor (empty `monitorId`) or one, animating through the engine
  // when enabled, `monitorId` is empty and `animate` is true.
  void setWallpaper(const QString& imagePath, const QString& monitorId = QString(),
                    bool animate = true);
  // Device paths of all monitors, in IDesktopWallpaper order.
  QStringList monitorIds();
  // Picks a random wallpaper (never the same twice in a row) unless `imagePath` is given. Ignored
  // while a previous change is still running.
  void changeBackground(const QString& imagePath = QString());

  const BackendFactory& backendFactory() const { return deps_.backend; }
  const QStringList& imagePaths() const { return imagePaths_; }
  bool isRunning() const { return running_; }
  bool timerRunning() const { return timerRunning_; }
  int timerIntervalMs() const { return timer_.interval(); }
  QString lastImage() const { return lastImage_; }
  WallpaperEngine* engine() const { return engine_; }

  Q_INVOKABLE void stop();

 private:
  platform::DesktopWallpaperBackend* backend();
  void runAfter(const QString& imagePath);
  void runNextCommand(const QString& imagePath, qsizetype index);

  Dependencies deps_;
  std::unique_ptr<platform::DesktopWallpaperBackend> backend_;
  QStringList imagePaths_;
  QStringList runAfter_;
  EngineOptions engineOptions_;
  QTimer timer_;
  bool timerRunning_ = false;
  bool running_ = false;
  bool skipLogged_ = false;
  QString lastImage_;
  QPointer<WallpaperEngine> engine_;
};

// The default command runner: `cmd.exe /c "<command>"` as a child QProcess.
WallpaperManager::CommandRunner systemCommandRunner(QObject* owner);

}  // namespace yasb::widgets

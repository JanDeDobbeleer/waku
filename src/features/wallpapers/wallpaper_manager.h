#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>
#include <random>

#include "features/wallpapers/desktop_wallpaper.h"

namespace waku::wallpapers {

// Port of services/wallpapers/manager.py (WallpaperManager). One per runtime, shared by every
// wallpapers widget through the polling-worker registry's service map (key "wallpapers"): the
// random-change timer, the "a change is running" guard and the post-change commands.
//
// Thread: GUI thread only; post-change commands run as async QProcess children. stop() (called by
// the registry at shutdown) stops the timer. The animated change is not here: the Qt Quick bar
// plays it itself (qml/wallpaper_transition.*) and then asks for the plain change.
class WallpaperManager : public QObject {
  Q_OBJECT
 public:
  using BackendFactory = std::function<std::unique_ptr<DesktopWallpaperBackend>()>;
  // Runs one post-change command line (cmd.exe semantics) and calls `done` with its stderr.
  using CommandRunner =
      std::function<void(const QString& command, std::function<void(const QString&)> done)>;
  // Index in [0, count) for the random wallpaper.
  using Picker = std::function<int(int count)>;

  struct Dependencies {
    BackendFactory backend;
    CommandRunner runner;
    Picker picker;
    // What the auto-change timer runs; null means changeBackground(). The Wallpapers service
    // passes its own random(), so a timed change is a manual one (lock screen, dry run, animation).
    std::function<void()> timedChange;
  };

  explicit WallpaperManager(Dependencies deps = {}, QObject* parent = nullptr);
  ~WallpaperManager() override;

  void configure(const QStringList& imagePaths, int updateInterval, bool changeAutomatically,
                 const QStringList& runAfter);

  // Sets the wallpaper on every monitor (empty `monitorId`) or one.
  void setWallpaper(const QString& imagePath, const QString& monitorId = QString());
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

  Q_INVOKABLE void stop();

 private:
  DesktopWallpaperBackend* backend();
  void runAfter(const QString& imagePath);
  void runNextCommand(const QString& imagePath, qsizetype index);

  Dependencies deps_;
  std::unique_ptr<DesktopWallpaperBackend> backend_;
  QStringList imagePaths_;
  QStringList runAfter_;
  QTimer timer_;
  bool timerRunning_ = false;
  bool running_ = false;
  QString lastImage_;
};

// The default command runner: `cmd.exe /c "<command>"` as a child QProcess.
WallpaperManager::CommandRunner systemCommandRunner(QObject* owner);

}  // namespace waku::wallpapers

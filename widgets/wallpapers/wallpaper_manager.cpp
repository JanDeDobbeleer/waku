#include "widgets/wallpapers/wallpaper_manager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <exception>
#include <memory>
#include <utility>

#include "widgets/wallpapers/wallpaper_files.h"

namespace yasb::widgets {

WallpaperManager::CommandRunner systemCommandRunner(QObject* owner) {
  QPointer<QObject> parent(owner);
  return [parent](const QString& command, std::function<void(const QString&)> done) {
    auto* process = new QProcess(parent.data());
    process->setProgram(QStringLiteral("cmd.exe"));
    // subprocess.run(shell=True) hands cmd.exe `/c "<command>"`.
    process->setNativeArguments(QStringLiteral("/c \"%1\"").arg(command));
    auto finish = [process, done = std::move(done)](const QString& stderrText) {
      process->deleteLater();
      done(stderrText);
    };
    auto delivered = std::make_shared<bool>(false);
    QObject::connect(process, &QProcess::finished, process, [process, finish, delivered] {
      if (std::exchange(*delivered, true)) return;
      finish(QString::fromUtf8(process->readAllStandardError()));
    });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, finish, delivered](QProcess::ProcessError error) {
                       if (error != QProcess::FailedToStart) return;
                       if (std::exchange(*delivered, true)) return;
                       finish(process->errorString());
                     });
    process->start();
  };
}

WallpaperManager::WallpaperManager(Dependencies deps, QObject* parent)
    : QObject(parent), deps_(std::move(deps)) {
  if (!deps_.backend) deps_.backend = [] { return platform::makeDesktopWallpaperBackend(); };
  if (!deps_.runner) deps_.runner = systemCommandRunner(this);
  if (!deps_.picker) {
    auto generator = std::make_shared<std::mt19937>(std::random_device{}());
    deps_.picker = [generator](int count) {
      return std::uniform_int_distribution<int>(0, count - 1)(*generator);
    };
  }
  connect(&timer_, &QTimer::timeout, this, [this] { changeBackground(); });
}

WallpaperManager::~WallpaperManager() { stop(); }

void WallpaperManager::stop() {
  timer_.stop();
  timerRunning_ = false;
}

platform::DesktopWallpaperBackend* WallpaperManager::backend() {
  if (!backend_) backend_ = deps_.backend();
  return backend_.get();
}

void WallpaperManager::configure(const QStringList& imagePaths, int updateInterval,
                                 bool changeAutomatically, const QStringList& runAfter) {
  imagePaths_ = imagePaths;
  runAfter_ = runAfter;
  if (changeAutomatically && !timerRunning_) {
    if (updateInterval > 0) {
      timer_.start(updateInterval * 1000);
      timerRunning_ = true;
    }
  } else if (!changeAutomatically && timerRunning_) {
    timer_.stop();
    timerRunning_ = false;
  }
}

void WallpaperManager::setWallpaper(const QString& imagePath, const QString& monitorId) {
  QString error;
  const QString absolute = QDir::toNativeSeparators(QFileInfo(imagePath).absoluteFilePath());
  if (!backend()->setWallpaper(monitorId, absolute, &error)) {
    qCritical().noquote() << "Failed to set wallpaper:" << error;
    running_ = false;
    return;
  }
  runAfter(imagePath);
}

QStringList WallpaperManager::monitorIds() {
  QStringList ids;
  try {
    for (const auto& monitor : backend()->monitors()) ids << monitor.id;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Failed to enumerate monitors:" << error.what();
  }
  return ids;
}

void WallpaperManager::changeBackground(const QString& imagePath) {
  if (running_) return;
  if (imagePaths_.isEmpty()) {
    qCritical() << "No image paths configured";
    return;
  }
  running_ = true;

  for (const QString& path : imagePaths_) {
    if (!QFileInfo::exists(path)) qWarning().noquote() << "Invalid image path:" << path;
  }

  // The same scan the galleries use, so a random wallpaper can be any file the gallery shows.
  const QStringList wallpapers = collectImageFiles(imagePaths_);
  if (wallpapers.isEmpty()) {
    qWarning().noquote() << "No wallpapers found in" << imagePaths_.join(QStringLiteral(", "));
    running_ = false;
    return;
  }

  QString next = imagePath;
  if (next.isEmpty()) {
    // Never the same wallpaper twice in a row.
    do {
      next = wallpapers.at(deps_.picker(static_cast<int>(wallpapers.size())));
    } while (next == lastImage_ && wallpapers.size() > 1);
  }

  try {
    setWallpaper(next);
    lastImage_ = next;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Error setting wallpaper" << next << ":" << error.what();
    running_ = false;
  }
}

void WallpaperManager::runAfter(const QString& imagePath) {
  if (runAfter_.isEmpty()) {
    running_ = false;
    return;
  }
  runNextCommand(imagePath, 0);
}

void WallpaperManager::runNextCommand(const QString& imagePath, qsizetype index) {
  if (index >= runAfter_.size()) {
    running_ = false;
    return;
  }
  QString command = runAfter_.at(index);
  command.replace(QStringLiteral("{image}"), QStringLiteral("\"%1\"").arg(imagePath));
  QPointer<WallpaperManager> self(this);
  deps_.runner(command, [self, imagePath, index](const QString& stderrText) {
    if (!self) return;
    if (!stderrText.isEmpty()) qCritical().noquote() << "error:" << stderrText;
    self->runNextCommand(imagePath, index + 1);
  });
}

}  // namespace yasb::widgets

#include "app/file_watcher.h"

#include <QFileInfo>
#include <utility>

namespace yasb::app {

FileWatcher::FileWatcher(QString configPath, QString stylesheetPath, Validator configIsValid,
                         Callback configChanged, Callback stylesheetChanged, int debounceMs,
                         bool watchConfig, bool watchStylesheet, QObject* parent)
    : QObject(parent),
      configPath_(std::move(configPath)),
      stylesheetPath_(std::move(stylesheetPath)),
      directory_(QFileInfo(configPath_).absolutePath()),
      configIsValid_(std::move(configIsValid)),
      configChanged_(std::move(configChanged)),
      stylesheetChanged_(std::move(stylesheetChanged)),
      configStamp_(stamp(configPath_)),
      stylesheetStamp_(stamp(stylesheetPath_)),
      watchConfig_(watchConfig),
      watchStylesheet_(watchStylesheet) {
  timer_.setSingleShot(true);
  timer_.setInterval(debounceMs);
  connect(&timer_, &QTimer::timeout, this, &FileWatcher::flush);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { inspectChanges(); });
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { inspectChanges(); });
  rearm();
}

FileWatcher::Stamp FileWatcher::stamp(const QString& path) {
  const QFileInfo info(path);
  return {info.exists(), info.exists() ? info.size() : -1,
          info.exists() ? info.lastModified() : QDateTime()};
}

void FileWatcher::rearm() {
  const QStringList files = watcher_.files();
  for (const QString& file : files) watcher_.removePath(file);
  if (watchConfig_ && QFileInfo::exists(configPath_)) watcher_.addPath(configPath_);
  if (watchStylesheet_ && QFileInfo::exists(stylesheetPath_)) watcher_.addPath(stylesheetPath_);
  if (QFileInfo::exists(directory_) && !watcher_.directories().contains(directory_))
    watcher_.addPath(directory_);
}

void FileWatcher::inspectChanges() {
  const Stamp configNow = stamp(configPath_);
  const Stamp stylesheetNow = stamp(stylesheetPath_);
  if (watchConfig_ && !(configNow == configStamp_)) configPending_ = true;
  if (watchStylesheet_ && !(stylesheetNow == stylesheetStamp_)) stylesheetPending_ = true;
  rearm();
  if (configPending_ || stylesheetPending_) timer_.start();
}

void FileWatcher::flush() {
  rearm();
  if (configPending_) {
    if (!configIsValid_ || configIsValid_()) {
      configPending_ = false;
      configStamp_ = stamp(configPath_);
      if (configChanged_) configChanged_();
    } else {
      timer_.start();
    }
  }
  if (stylesheetPending_) {
    stylesheetPending_ = false;
    stylesheetStamp_ = stamp(stylesheetPath_);
    if (stylesheetChanged_) stylesheetChanged_();
  }
}

}  // namespace yasb::app

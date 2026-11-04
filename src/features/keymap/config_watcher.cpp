#include "features/keymap/config_watcher.h"

#include <QDir>
#include <QFileInfo>

namespace waku::keymap {

ConfigWatcher::ConfigWatcher(QString path, std::function<void()> changed, int debounceMs,
                             QObject* parent)
    : QObject(parent), path_(std::move(path)), changed_(std::move(changed)) {
  timer_.setSingleShot(true);
  timer_.setInterval(debounceMs);
  connect(&timer_, &QTimer::timeout, this, [this] {
    rearm();
    if (changed_) changed_();
  });
  const auto onEvent = [this] {
    rearm();
    timer_.start();
  };
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, onEvent);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, onEvent);
  rearm();
}

void ConfigWatcher::rearm() {
  const QFileInfo info(path_);
  if (!watcher_.directories().contains(info.absolutePath()) && info.dir().exists()) {
    watcher_.addPath(info.absolutePath());
  }
  // A replaced file is a new file: its watch must be added again.
  if (info.exists() && !watcher_.files().contains(info.absoluteFilePath())) {
    watcher_.addPath(info.absoluteFilePath());
  }
}

}  // namespace waku::keymap

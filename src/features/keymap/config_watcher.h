#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

namespace waku::keymap {

// Calls `changed` once, `debounceMs` after the last change of a file. It watches the file and its
// folder, because an editor that saves by writing a new file and renaming it drops the watch on the
// file. GUI thread (it needs the event loop).
class ConfigWatcher : public QObject {
 public:
  ConfigWatcher(QString path, std::function<void()> changed, int debounceMs = 400,
                QObject* parent = nullptr);

 private:
  void rearm();

  QString path_;
  std::function<void()> changed_;
  QFileSystemWatcher watcher_;
  QTimer timer_;
};

}  // namespace waku::keymap

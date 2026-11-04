#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <functional>
#include <utility>
#include <vector>

#include "features/keymap/keymap_system.h"
#include "features/keymap/keymap_types.h"
#include "kernel/qt/hotkeys.h"

namespace waku::keymap {

// The hotkey table of `wakuc keys`: loads a whkd file, registers its bindings in the kernel
// Hotkeys table (RegisterHotKey) and runs the action of a binding when its key is pressed.
//
// A bad file never replaces a good one: a file that cannot be read, or whose every binding is
// bad, leaves the previous bindings in place. A file with some bad lines loads the good ones.
// A combination another program owns is a conflict: logged once, counted, not fatal.
//
// Threading: GUI thread (the event loop of the daemon). Shell launches and control-pipe calls run
// on the executor, which calls `log` from its own threads, so `log` must be thread safe.
class KeymapDaemon : public QObject {
 public:
  struct Deps {
    ProcessLauncher* launcher = nullptr;
    WindowSystem* windows = nullptr;
    ControlClient* bar = nullptr;
    Executor executor;                        // default: the global QThreadPool
    std::function<void(const QString&)> log;  // one line, no newline
    std::function<void()> stop;               // `waku:keys-stop`: ends the daemon
  };

  struct LoadResult {
    bool applied = false;
    int bindings = 0;
    int conflicts = 0;
    int issues = 0;
    QString message;  // one line: "ok: ..." or "error: ..."
  };

  explicit KeymapDaemon(Deps deps, QObject* parent = nullptr);
  ~KeymapDaemon() override;

  // Replaces the bindings with those of `text`; see the class comment.
  LoadResult load(const QString& text);
  // Reads `path` (UTF-8) and loads it. It becomes the file reloadFile() reads.
  LoadResult loadFile(const QString& path);
  // Reads the file of the last loadFile() again. With `onlyIfChanged` a file with the same text as
  // the one in use is left alone (the file watcher), and the result says so.
  LoadResult reloadFile(bool onlyIfChanged = false);

  // The bindings in the file, and how many of them are registered.
  int bindingCount() const { return static_cast<int>(keymap_.bindings.size()); }
  int boundCount() const { return bound_; }
  int conflictCount() const { return conflicts_; }
  int issueCount() const { return issues_; }
  bool paused() const { return paused_; }
  const QString& path() const { return path_; }
  // "bindings=3; conflicts=1; issues=0; paused=no".
  QString statusText() const;

  // A key was pressed: runs the action of binding `index` (tests call this too).
  void trigger(int index);

 private:
  void unregisterAll();
  void togglePause();
  void run(const QString& command, const QString& keys);
  void launch(const QString& command, const QString& keys);
  void post(std::function<void()> job);
  void log(const QString& line) const;

  Deps deps_;
  Keymap keymap_;
  std::vector<kernel::Hotkeys::Handle> handles_;
  QSet<quint64> reportedConflicts_;  // combinations already reported as taken
  QString path_;
  QString lastText_;
  bool haveText_ = false;
  int bound_ = 0;
  int conflicts_ = 0;
  int issues_ = 0;
  bool paused_ = false;
};

}  // namespace waku::keymap

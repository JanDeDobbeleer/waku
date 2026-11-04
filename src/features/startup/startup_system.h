#pragma once

#include <QString>

namespace waku::startup {

// Everything `wakuc startup` needs from Windows, behind one interface so the logic in
// startup_service.h runs against a fake (testing/fake_startup_system.h). The Windows
// implementation is win32/startup_system_win.cpp (Task Scheduler COM API, documented).
class StartupSystem {
 public:
  virtual ~StartupSystem() = default;

  // The full path `exeName` (for example "waku.exe") resolves to through the PATH of the current
  // user, the way the logon task will find it. Empty when it is not on the PATH.
  virtual QString findOnPath(const QString& exeName) const = 0;

  // The folder of the running wakuc.exe. waku.exe sits next to it.
  virtual QString ownFolder() const = 0;

  struct TaskInfo {
    bool registered = false;
    bool enabled = false;
    QString arguments;  // the command line of the task action, without the program name
  };
  virtual TaskInfo queryTask() const = 0;

  // Replaces the logon task of the current user: trigger at logon with no delay, Limited run
  // level, priority 4, no execution time limit, one instance, runs on battery. The action is the
  // bare `waku.exe` (resolved through the PATH at logon) with `arguments` as its command line.
  // Returns an empty string on success, else a one-line reason.
  virtual QString registerTask(const QString& arguments) = 0;

  // Removes the logon task. Succeeds when there is none. Same return value as registerTask.
  virtual QString removeTask() = 0;

  // The old way: a waku.lnk in the Startup folder. Deletes it and returns true when there was one.
  // Nothing is kept: a backup file left in the Startup folder makes Windows ask which app opens it.
  virtual bool removeShortcut() = 0;
};

}  // namespace waku::startup

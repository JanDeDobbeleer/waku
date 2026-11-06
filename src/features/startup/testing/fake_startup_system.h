#pragma once

#include <QString>
#include <QStringList>

#include "features/startup/startup_system.h"

namespace waku::startup {

// Test double of StartupSystem: the PATH result, the task and the shortcut are plain fields, every
// mutating call is recorded.
class FakeStartupSystem final : public StartupSystem {
 public:
  QString pathResult = QStringLiteral("C:\\Apps\\waku\\waku.exe");
  QString own = QStringLiteral("C:\\Apps\\waku");
  QString keysPathResult = QStringLiteral("C:\\Apps\\waku\\wakukeys.exe");
  QString keysConfig = QStringLiteral("C:\\Users\\me\\.config\\waku\\keys.conf");
  bool keysConfigPresent = false;
  TaskInfo task;      // the bar task
  TaskInfo keysTask;  // the keys daemon task
  bool shortcutPresent = false;
  QString registerError;
  QString keysRegisterError;
  QString removeError;

  QStringList registeredArguments;      // one entry per bar registerTask call
  QStringList keysRegisteredArguments;  // one entry per keys registerTask call
  int removeCalls = 0;
  int keysRemoveCalls = 0;
  int removeShortcutCalls = 0;

  QString findOnPath(const QString& exeName) const override {
    return exeName == QStringLiteral("wakukeys.exe") ? keysPathResult : pathResult;
  }
  QString ownFolder() const override { return own; }
  QString keysConfigPath(const QString&) const override { return keysConfig; }
  bool keysConfigExists(const QString&) const override { return keysConfigPresent; }
  TaskInfo queryTask(Task which) const override { return which == Task::Keys ? keysTask : task; }

  QString registerTask(Task which, const QString& arguments) override {
    const bool keys = which == Task::Keys;
    (keys ? keysRegisteredArguments : registeredArguments) << arguments;
    const QString& error = keys ? keysRegisterError : registerError;
    if (!error.isEmpty()) return error;
    TaskInfo& target = keys ? keysTask : task;
    target.registered = true;
    target.enabled = true;
    target.arguments = arguments;
    return {};
  }
  QString removeTask(Task which) override {
    const bool keys = which == Task::Keys;
    ++(keys ? keysRemoveCalls : removeCalls);
    if (!removeError.isEmpty()) return removeError;
    (keys ? keysTask : task) = TaskInfo{};
    return {};
  }
  bool removeShortcut() override {
    ++removeShortcutCalls;
    const bool had = shortcutPresent;
    shortcutPresent = false;
    return had;
  }
};

}  // namespace waku::startup

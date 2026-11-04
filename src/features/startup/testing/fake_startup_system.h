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
  TaskInfo task;
  bool shortcutPresent = false;
  QString registerError;
  QString removeError;

  QStringList registeredArguments;  // one entry per registerTask call
  int removeCalls = 0;
  int removeShortcutCalls = 0;

  QString findOnPath(const QString&) const override { return pathResult; }
  QString ownFolder() const override { return own; }
  TaskInfo queryTask() const override { return task; }

  QString registerTask(const QString& arguments) override {
    registeredArguments << arguments;
    if (!registerError.isEmpty()) return registerError;
    task.registered = true;
    task.enabled = true;
    task.arguments = arguments;
    return {};
  }
  QString removeTask() override {
    ++removeCalls;
    if (!removeError.isEmpty()) return removeError;
    task = TaskInfo{};
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

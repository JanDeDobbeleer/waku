#pragma once

// Header-only fakes for tests (rule S5). Nothing here starts a process, moves a window or opens a
// pipe, so a test never touches the user's desktop.

#include <QString>
#include <QStringList>
#include <vector>

#include "features/keymap/keymap_system.h"

namespace waku::keymap {

class FakeProcessLauncher final : public ProcessLauncher {
 public:
  bool launch(const ShellInvocation& invocation) override {
    launched.push_back(invocation);
    return succeed;
  }
  std::vector<ShellInvocation> launched;
  bool succeed = true;
};

class FakeWindowSystem final : public WindowSystem {
 public:
  QString foregroundProcessName() override { return foreground; }
  bool activateWindow(const QString& target) override {
    activated.push_back(target);
    return hasWindow;
  }
  void allowForeground(unsigned long pid) override { allowed.push_back(pid); }

  QString foreground;
  bool hasWindow = false;
  QStringList activated;
  std::vector<unsigned long> allowed;
};

class FakeControlClient final : public ControlClient {
 public:
  control::ControlReply send(const QString& line, int timeoutMs) override {
    lines.push_back(line);
    timeouts.push_back(timeoutMs);
    control::ControlReply reply;
    reply.status = control::ControlReply::Ok;
    reply.text = line == QLatin1String("status") ? statusText : answer;
    return reply;
  }
  QStringList lines;
  std::vector<int> timeouts;
  QString answer = QStringLiteral("ok");
  QString statusText = QStringLiteral("running; pid=4242; windows=1");
};

// Runs a job at once, on the calling thread.
inline Executor immediateExecutor() {
  return [](std::function<void()> job) { job(); };
}

}  // namespace waku::keymap

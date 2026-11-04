#pragma once

#include <QString>
#include <functional>
#include <memory>

#include "control/control.h"
#include "features/keymap/keymap_action.h"

namespace waku::keymap {

// What the daemon needs from the machine. The Windows versions are in win32/ and PipeControlClient
// below; tests use testing/keymap_fakes.h, so a test never starts a process, moves a window or
// talks to a real bar.

// Starts a shell command without a window and does not wait for it. May be called from a worker
// thread.
class ProcessLauncher {
 public:
  virtual ~ProcessLauncher() = default;
  virtual bool launch(const ShellInvocation& invocation) = 0;
};

// Windows (the top-level windows of the desktop). GUI thread.
class WindowSystem {
 public:
  virtual ~WindowSystem() = default;
  // The executable of the foreground window ("firefox.exe"), "" when there is none or it cannot
  // be read (an elevated process).
  virtual QString foregroundProcessName() = 0;
  // Brings the visible top-level window of that executable, or else the one whose title contains
  // `target`, to the front (restoring it if minimized). False when there is none.
  virtual bool activateWindow(const QString& target) = 0;
  // AllowSetForegroundWindow for `pid`, so the bar can take the focus for a window it shows.
  virtual void allowForeground(unsigned long pid) = 0;
};

// The control pipe of the bar. Blocking: the daemon calls it from a worker thread.
class ControlClient {
 public:
  virtual ~ControlClient() = default;
  virtual control::ControlReply send(const QString& line, int timeoutMs) = 0;
};

// Sends to the pipe of one bar (control.h, sendControlCommand).
class PipeControlClient final : public ControlClient {
 public:
  explicit PipeControlClient(QString pipeName) : pipeName_(std::move(pipeName)) {}
  control::ControlReply send(const QString& line, int timeoutMs) override {
    return control::sendControlCommand(pipeName_, line, timeoutMs);
  }

 private:
  QString pipeName_;
};

// Runs a job off the GUI thread. The default uses the global QThreadPool.
using Executor = std::function<void(std::function<void()>)>;
Executor poolExecutor();

// The Windows implementations (win32/keymap_system_win.cpp).
std::unique_ptr<ProcessLauncher> makeWin32ProcessLauncher();
std::unique_ptr<WindowSystem> makeWin32WindowSystem();

}  // namespace waku::keymap

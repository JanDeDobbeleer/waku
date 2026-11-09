#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <memory>
#include <utility>

#include "features/tray/tray_types.h"

namespace waku::tray {

// Where tray icon notifications come from. The real implementation (`SystrayHook`) intercepts
// Explorer's Shell_TrayWnd; widgets and tests only see this interface,
// so a test substitutes a fake and never touches the real tray.
//
// Signals are emitted from the implementation's worker thread; connect with the default (auto)
// connection from a GUI-thread receiver and they arrive queued.
class TrayHost : public QObject {
  Q_OBJECT
 public:
  explicit TrayHost(QObject* parent = nullptr);

  // Starts delivering icons (spawns the worker). Safe to call twice.
  virtual void start() = 0;
  // Stops the worker, releases every OS resource and returns once the worker has ended. Safe to
  // call twice and from the destructor of the derived class.
  Q_INVOKABLE virtual void stop() = 0;
  // "hook", "legacy" or a fake's name; for logs and tests.
  virtual QString mode() const = 0;
  // Announces every icon the host already knows, as iconModified, on the calling thread. A host
  // that outlives its widget (the window host, across a reload) gives a new widget its icons
  // without making every program send them again. Nothing for a host that knows none.
  virtual void replay() {}

 signals:
  // The tray should be asked to re-announce its icons (TaskbarCreated broadcast).
  void updateIcons();
  void iconModified(const waku::tray::TrayIconData& data);
  void iconDeleted(const waku::tray::TrayIconData& data);
};

// The Win32 calls a tray icon button makes when the user clicks it, plus the TaskbarCreated
// broadcast. Faked in tests so no message ever reaches a real window.
class TrayWindowApi {
 public:
  virtual ~TrayWindowApi() = default;
  virtual bool isWindow(std::uint64_t hwnd) = 0;
  // AllowSetForegroundWindow for the process that owns `hwnd`.
  virtual void allowSetForeground(std::uint64_t hwnd) = 0;
  virtual void sendNotifyMessage(std::uint64_t hwnd, std::uint32_t message, std::uint64_t wParam,
                                 std::int64_t lParam) = 0;
  virtual std::pair<int, int> cursorPos() = 0;
  // RegisterWindowMessage("TaskbarCreated") broadcast with HWND_BROADCAST.
  virtual void broadcastTaskbarCreated() = 0;
};

// The Win32-backed implementation (stateless).
std::unique_ptr<TrayWindowApi> makeTrayWindowApi();

}  // namespace waku::tray

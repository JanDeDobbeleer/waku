#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

namespace waku::workspaces::vdesk {

// The registry key Explorer keeps the desktop list, the shown desktop and the names in.
inline constexpr wchar_t kVirtualDesktopsKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";

// One binary value of that key (VirtualDesktopIDs, CurrentVirtualDesktop), or nothing.
std::optional<QByteArray> readVirtualDesktopsValue(const wchar_t* name);

// Fires `onChange` (GUI thread) when anything under the key changes. Backed by
// RegNotifyChangeKeyValue and a QWinEventNotifier; re-armed after each event. Stops with the
// object.
class RegistryChangeWatcher {
 public:
  RegistryChangeWatcher();
  ~RegistryChangeWatcher();
  RegistryChangeWatcher(const RegistryChangeWatcher&) = delete;
  RegistryChangeWatcher& operator=(const RegistryChangeWatcher&) = delete;
  // False when the key does not exist or the wait could not be set up.
  bool start(std::function<void()> onChange);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// The undocumented shell interface behind Task View (IVirtualDesktopManagerInternal, reached
// through the ImmersiveShell service provider in explorer.exe). Windows 11 build 26100 and later
// only. The allowlist row in docs/architecture.md and the "Shell interface" section of
// features/workspaces/design.md say what was verified and what was not.
//
// Desktops are named by their GUID in registry form ({XXXXXXXX-...}, upper case), never by an
// index: the registry list and the shell's list could differ for a moment.
//
// Threading: the GUI thread (an STA) only. Every call is a call into explorer.exe: it blocks while
// Explorer hangs and fails with 0x800706BA when Explorer restarts (lost() is then true; drop the
// object and open a new one). Every call is guarded against SEH and C++ exceptions and reports
// failure as false or nullopt, logging each failing call once.
class ShellDesktopApi {
 public:
  ~ShellDesktopApi();
  ShellDesktopApi(const ShellDesktopApi&) = delete;
  ShellDesktopApi& operator=(const ShellDesktopApi&) = delete;

  // Opens and self-tests the interface (read-only calls). Null with `reason` set when it is not
  // there or does not look as expected; nothing else changes then.
  static std::unique_ptr<ShellDesktopApi> open(QString* reason);

  // True after a call failed because Explorer went away.
  bool lost() const;

  // The desktop ids in the shell's order.
  std::optional<QStringList> desktopIds();
  // The shown desktop.
  std::optional<QString> currentId();
  // Shows the desktop; animated like Win+Ctrl+Arrow, or at once.
  bool switchTo(const QString& id, bool animate);
  // Creates a desktop without showing it; its id.
  std::optional<QString> create();
  // Closes `id`; its windows go to `fallbackId`. Like Task View's close button. The first mutating
  // call of a run re-verifies the desktop list against the registry and refuses when they differ.
  bool remove(const QString& id, const QString& fallbackId);
  bool rename(const QString& id, const QString& name);

  // Registers for the shell's desktop notifications. `changed` runs, on the GUI thread, after any
  // desktop event; `destroyBegin` runs after a desktop is closed with the ids of the closed desktop
  // and of the one that receives its windows (debug aid). Both are posted from the handler, never
  // called inside it. False when the registration failed.
  bool registerNotifications(std::function<void()> changed,
                             std::function<void(QString, QString)> destroyBegin);

 private:
  struct Impl;
  explicit ShellDesktopApi(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace waku::workspaces::vdesk

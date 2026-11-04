#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>
#include <optional>
#include <vector>

namespace waku::workspaces::vdesk {

// One Windows virtual desktop.
struct Desktop {
  QString id;    // the desktop GUID in registry form: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
  QString name;  // what the user named it; empty when it has the default name
  // HWNDs of the windows on this desktop, ascending (a stable order, so an unchanged desktop
  // compares equal). The count is windows.size().
  std::vector<quint64> windows;

  friend bool operator==(const Desktop&, const Desktop&) = default;
};

// The desktops in Task View order and the index of the one shown.
struct State {
  std::vector<Desktop> desktops;
  int current = -1;

  friend bool operator==(const State&, const State&) = default;
};

// Splits the registry value `VirtualDesktopIDs` (or the 16 bytes of `CurrentVirtualDesktop`) into
// GUID strings. A trailing partial entry is dropped.
QStringList parseDesktopIds(const QByteArray& bytes);

// Platform access. Replaceable so tests never talk to the live desktop.
class VirtualDesktopSystem {
 public:
  virtual ~VirtualDesktopSystem() = default;
  // The desktops now, or nothing when Windows does not expose them (the registry keys are gone,
  // as on a Windows build without virtual desktops).
  virtual std::optional<State> read() = 0;
  // Shows the desktop at `index` in one jump: a window of this process is moved to that desktop
  // and activated (documented IVirtualDesktopManager::MoveWindowToDesktop), and Windows switches
  // to it. False when that is not possible (no foreground right, the move failed). The change
  // shows up in the next read().
  virtual bool jumpTo(int index) = 0;
  // Creates one desktop with Win+Ctrl+D (Windows has no documented call for it); Windows shows the
  // new desktop. Fire and forget.
  virtual void create() = 0;
};

// The real system (win32/virtual_desktops_win.cpp).
std::unique_ptr<VirtualDesktopSystem> makeWin32VirtualDesktopSystem();

}  // namespace waku::workspaces::vdesk

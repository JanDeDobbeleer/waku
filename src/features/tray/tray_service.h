#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

#include "features/tray/tray_host.h"

namespace waku::tray {

// How `createTrayHost` builds each kind of host; the defaults are the real ones, tests count
// calls instead of touching Explorer.
struct TrayHostFactories {
  // Path of the hook DLL for this machine, empty (with `reason`) when the hook cannot be used.
  std::function<QString(const QString& appDir, QString* reason)> findHookDll;
  // A hook host for `dllPath`; null for a host that is not valid.
  std::function<std::unique_ptr<TrayHost>(const QString& dllPath)> makeHook;
  // The window host; null for a host that is not valid (another waku of the session has it).
  std::function<std::unique_ptr<TrayHost>()> makeWindow;
};

// Which tray host a run uses: the hook DLL in Explorer, or a Shell_TrayWnd window of waku's own
// (win32/tray_window_host.h). Set once at start-up by main (`--tray-host`), read when the first
// Tray widget creates the host.
enum class TrayHostKind { Hook, Window };
TrayHostKind trayHostKind();
void setTrayHostKind(TrayHostKind kind);

// The factories wired to SystrayHook.
TrayHostFactories realTrayHostFactories();

// The tray client: the hook when `useHook` is set and usable, null otherwise (a missing DLL is
// logged as a warning, emulation and unsupported architectures at debug level). There is no
// fallback that replaces Explorer's tray window.
std::unique_ptr<TrayHost> createTrayHost(bool useHook, const QString& appDir,
                                         const TrayHostFactories& factories);
// The host of `kind`: the same as above for Hook; for Window the window host, which needs no DLL.
std::unique_ptr<TrayHost> createTrayHost(TrayHostKind kind, const QString& appDir,
                                         const TrayHostFactories& factories);

// The one tray client every systray widget of a run shares (Python keeps it in class attributes
// of SystrayWidget). Created on first use through the polling-worker registry's service map
// under key "systray", owned by that registry, stopped by its stopAll().
//
// Threads: constructed and used on the GUI thread; the host owns its worker thread (see
// features/tray/win32/tray_hook.h).
class SystrayService : public QObject {
  Q_OBJECT
 public:
  SystrayService(bool useHook, const QString& appDir, const TrayHostFactories& factories,
                 QObject* parent = nullptr);

  TrayHost* host() const { return host_.get(); }
  TrayWindowApi* windowApi() const { return api_.get(); }
  // True for the first widget that asks: only one widget answers the "re-announce your icons"
  // request, as in Python (`_systray_refresh_signal`).
  bool claimRefreshSignal();

  Q_INVOKABLE void stop();

 private:
  std::unique_ptr<TrayHost> host_;
  std::unique_ptr<TrayWindowApi> api_;
  bool refreshClaimed_ = false;
};

}  // namespace waku::tray

#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

#include "platform/systray_host.h"

namespace yasb::widgets {

// How `createTrayHost` builds each kind of host; the defaults are the real ones, tests count
// calls instead of touching Explorer.
struct TrayHostFactories {
  // Path of the hook DLL for this machine, empty (with `reason`) when the hook cannot be used.
  std::function<QString(const QString& appDir, QString* reason)> findHookDll;
  // A hook host for `dllPath`; null or invalid hosts fall back to legacy mode.
  std::function<std::unique_ptr<platform::TrayHost>(const QString& dllPath)> makeHook;
  std::function<std::unique_ptr<platform::TrayHost>()> makeLegacy;
};

// The factories wired to platform::SystrayHook and platform::SystrayLegacy.
TrayHostFactories realTrayHostFactories();

// SystrayWidget.setup_client's choice of client: the hook when `useHook` is set and usable, the
// legacy window otherwise (a missing DLL is logged once as a warning, emulation and unsupported
// architectures at debug level).
std::unique_ptr<platform::TrayHost> createTrayHost(bool useHook, const QString& appDir,
                                                   const TrayHostFactories& factories);

// The one tray client every systray widget of a run shares (Python keeps it in class attributes
// of SystrayWidget). Created on first use through the polling-worker registry's service map
// under key "systray", owned by that registry, stopped by its stopAll().
//
// Threads: constructed and used on the GUI thread; the host owns its worker thread (see
// platform/systray_hook.h and systray_legacy.h).
class SystrayService : public QObject {
  Q_OBJECT
 public:
  SystrayService(bool useHook, const QString& appDir, const TrayHostFactories& factories,
                 QObject* parent = nullptr);

  platform::TrayHost* host() const { return host_.get(); }
  platform::TrayWindowApi* windowApi() const { return api_.get(); }
  // True for the first widget that asks: only one widget answers the "re-announce your icons"
  // request, as in Python (`_systray_refresh_signal`).
  bool claimRefreshSignal();

  Q_INVOKABLE void stop();

 private:
  std::unique_ptr<platform::TrayHost> host_;
  std::unique_ptr<platform::TrayWindowApi> api_;
  bool refreshClaimed_ = false;
};

}  // namespace yasb::widgets

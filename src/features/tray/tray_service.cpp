#include "features/tray/tray_service.h"

#include <QCoreApplication>
#include <QDebug>
#include <atomic>

#include "features/tray/win32/tray_hook.h"
#include "features/tray/win32/tray_window_host.h"

namespace waku::tray {

namespace {
std::atomic<TrayHostKind> g_kind{TrayHostKind::Hook};
}  // namespace

TrayHostKind trayHostKind() { return g_kind.load(); }
void setTrayHostKind(TrayHostKind kind) { g_kind = kind; }

TrayHostFactories realTrayHostFactories() {
  TrayHostFactories factories;
  factories.findHookDll = [](const QString& appDir, QString* reason) {
    return findTrayHookDll(appDir, reason);
  };
  factories.makeHook = [](const QString& dllPath) -> std::unique_ptr<TrayHost> {
    SystrayHook::Options options;
    options.injector = makeExplorerInjector(dllPath);
    auto hook = std::make_unique<SystrayHook>(std::move(options));
    if (!hook->valid()) return nullptr;
    return hook;
  };
  factories.makeWindow = []() -> std::unique_ptr<TrayHost> {
    auto host = std::make_unique<TrayWindowHost>();
    if (!host->valid()) return nullptr;
    return host;
  };
  return factories;
}

std::unique_ptr<TrayHost> createTrayHost(bool useHook, const QString& appDir,
                                         const TrayHostFactories& factories) {
  if (useHook && factories.findHookDll && factories.makeHook) {
    QString reason;
    const QString dll = factories.findHookDll(appDir, &reason);
    if (dll.isEmpty()) {
      if (reason.contains(QLatin1String("not found")))
        qWarning().noquote() << "Systray hook DLL missing. No tray host.";
      else
        qDebug().noquote() << "Systray hook disabled (" << reason << "); no tray host.";
    } else if (auto hook = factories.makeHook(dll)) {
      return hook;
    } else {
      qWarning().noquote() << "Systray hook could not start. No tray host.";
    }
  }
  return nullptr;
}

std::unique_ptr<TrayHost> createTrayHost(TrayHostKind kind, const QString& appDir,
                                         const TrayHostFactories& factories) {
  if (kind == TrayHostKind::Hook) return createTrayHost(true, appDir, factories);
  if (!factories.makeWindow) return nullptr;
  auto host = factories.makeWindow();
  if (!host) qWarning().noquote() << "Tray window host could not start (another one is running).";
  return host;
}

SystrayService::SystrayService(bool useHook, const QString& appDir,
                               const TrayHostFactories& factories, QObject* parent)
    : QObject(parent),
      host_(createTrayHost(useHook, appDir, factories)),
      api_(makeTrayWindowApi()) {}

bool SystrayService::claimRefreshSignal() {
  if (refreshClaimed_) return false;
  refreshClaimed_ = true;
  return true;
}

void SystrayService::stop() {
  if (host_) host_->stop();
}

}  // namespace waku::tray

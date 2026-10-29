#include "widgets/systray/systray_service.h"

#include <QCoreApplication>
#include <QDebug>

#include "platform/systray_hook.h"

namespace yasb::widgets {

TrayHostFactories realTrayHostFactories() {
  TrayHostFactories factories;
  factories.findHookDll = [](const QString& appDir, QString* reason) {
    return platform::findTrayHookDll(appDir, reason);
  };
  factories.makeHook = [](const QString& dllPath) -> std::unique_ptr<platform::TrayHost> {
    platform::SystrayHook::Options options;
    options.injector = platform::makeExplorerInjector(dllPath);
    auto hook = std::make_unique<platform::SystrayHook>(std::move(options));
    if (!hook->valid()) return nullptr;
    return hook;
  };
  return factories;
}

std::unique_ptr<platform::TrayHost> createTrayHost(bool useHook, const QString& appDir,
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

SystrayService::SystrayService(bool useHook, const QString& appDir,
                               const TrayHostFactories& factories, QObject* parent)
    : QObject(parent),
      host_(createTrayHost(useHook, appDir, factories)),
      api_(platform::makeTrayWindowApi()) {}

bool SystrayService::claimRefreshSignal() {
  if (refreshClaimed_) return false;
  refreshClaimed_ = true;
  return true;
}

void SystrayService::stop() {
  if (host_) host_->stop();
}

}  // namespace yasb::widgets

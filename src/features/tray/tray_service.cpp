#include "features/tray/tray_service.h"

#include <QDebug>

#include "features/tray/win32/tray_window_host.h"

namespace waku::tray {

TrayHostFactories realTrayHostFactories() {
  TrayHostFactories factories;
  factories.makeWindow = []() -> std::unique_ptr<TrayHost> {
    auto host = std::make_unique<TrayWindowHost>();
    if (!host->valid()) return nullptr;
    return host;
  };
  return factories;
}

std::unique_ptr<TrayHost> createTrayHost(const TrayHostFactories& factories) {
  if (!factories.makeWindow) return nullptr;
  auto host = factories.makeWindow();
  if (!host) qWarning().noquote() << "Tray window host could not start (another one is running).";
  return host;
}

}  // namespace waku::tray

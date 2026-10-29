#include "spike/services/systray.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlEngine>
#include <utility>

#include "platform/systray_click.h"
#include "platform/systray_types.h"
#include "spike/services/tray_image_provider.h"
#include "widgets/systray/systray_service.h"

namespace tidy::services {
namespace {

constexpr int kRefreshDelayMs = 200;    // SystrayWidget's refreshTimer_
constexpr int kSweepIntervalMs = 5000;  // SystrayWidget's iconCheckTimer_

struct RealHost {
  std::unique_ptr<platform::TrayHost> host;
  QString reason;
};

bool g_disabled = false;

// The hook host or nothing: the legacy window is never created (it would take over Explorer's
// tray window), so makeLegacy is cleared.
RealHost makeRealHost() {
  RealHost result;
  if (g_disabled) {
    result.reason = QStringLiteral("tray unavailable: disabled by --no-tray");
    return result;
  }
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
    result.reason = QStringLiteral("tray unavailable: offscreen platform");
    return result;
  }
  yasb::widgets::TrayHostFactories factories = yasb::widgets::realTrayHostFactories();
  factories.makeLegacy = nullptr;
  QString dllReason;
  bool hookTried = false;
  auto findDll = factories.findHookDll;
  factories.findHookDll = [&](const QString& appDir, QString* reason) {
    const QString dll = findDll(appDir, reason);
    if (reason) dllReason = *reason;
    return dll;
  };
  auto makeHook = factories.makeHook;
  factories.makeHook = [&](const QString& dll) {
    hookTried = true;
    return makeHook(dll);
  };
  try {
    result.host =
        yasb::widgets::createTrayHost(true, QCoreApplication::applicationDirPath(), factories);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray host failed:" << error.what();
  }
  if (!result.host) {
    if (!dllReason.isEmpty()) {
      result.reason = QStringLiteral("tray unavailable: %1").arg(dllReason);
    } else if (hookTried) {
      result.reason = QStringLiteral("tray unavailable: another tray host is running");
    } else {
      result.reason = QStringLiteral("tray unavailable: hook not usable");
    }
  }
  return result;
}

std::function<std::unique_ptr<Systray>()>& testFactory() {
  static std::function<std::unique_ptr<Systray>()> factory;
  return factory;
}

QSet<QUuid> systemGuids() {
  return {platform::kTrayBatteryIconGuid, platform::kTrayVolumeIconGuid,
          platform::kTrayNetworkIconGuid};
}

}  // namespace

Systray::Systray(QObject* parent) : QObject(parent) {
  RealHost real = makeRealHost();
  host_ = std::move(real.host);
  reason_ = std::move(real.reason);
  api_ = platform::makeTrayWindowApi();
  registry_.setFilter(systemGuids(), {});
  connectHost();
}

Systray::Systray(std::unique_ptr<platform::TrayHost> host,
                 std::unique_ptr<platform::TrayWindowApi> api, QString unavailableReason,
                 QObject* parent)
    : QObject(parent),
      host_(std::move(host)),
      api_(std::move(api)),
      reason_(std::move(unavailableReason)) {
  registry_.setFilter(systemGuids(), {});
  connectHost();
}

Systray::~Systray() {
  if (host_) host_->stop();
}

void Systray::connectHost() {
  if (!host_) return;
  // The host emits from its worker thread; the automatic connection queues onto this thread.
  connect(host_.get(), &platform::TrayHost::iconModified, this,
          [this](const platform::TrayIconData& data) { onIconModified(data); });
  connect(host_.get(), &platform::TrayHost::iconDeleted, this,
          [this](const platform::TrayIconData& data) { onIconDeleted(data); });
  // The hook has just connected: have Explorer announce every icon it already has.
  refreshTimer_.setSingleShot(true);
  refreshTimer_.setInterval(kRefreshDelayMs);
  connect(host_.get(), &platform::TrayHost::updateIcons, this, [this] { refreshTimer_.start(); });
  connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
  connect(&sweepTimer_, &QTimer::timeout, this, [this] { sweep(); });
  sweepTimer_.start(kSweepIntervalMs);
  host_->start();
}

Systray* Systray::create(QQmlEngine* engine, QJSEngine*) {
  // The engine owns the returned singleton (no CppOwnership), so the provider's model outlives
  // nothing it should not: the engine deletes its providers first.
  Systray* systray = nullptr;
  if (testFactory()) {
    systray = testFactory()().release();
  }
  if (!systray) systray = new Systray(nullptr);
  if (engine) {
    engine->addImageProvider(QStringLiteral("tray"), new TrayImageProvider(systray->icons()));
  }
  return systray;
}

void Systray::setFactoryForTests(std::function<std::unique_ptr<Systray>()> factory) {
  testFactory() = std::move(factory);
}

void Systray::setDisabled(bool disabled) { g_disabled = disabled; }

void Systray::setFilter(bool showSystemIcons, const QStringList& hiddenExeNames) {
  registry_.setFilter(showSystemIcons ? QSet<QUuid>() : systemGuids(), hiddenExeNames);
}

void Systray::onIconModified(const platform::TrayIconData& data) {
  try {
    const auto result = registry_.modified(data);
    if (result.change == yasb::widgets::TrayIconRegistry::Change::Ignored) return;
    if (const platform::TrayIconData* stored = registry_.data(result.id)) {
      model_.apply(result.id, *stored);
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray icon update failed:" << error.what();
  }
}

void Systray::onIconDeleted(const platform::TrayIconData& data) {
  try {
    if (const auto id = registry_.deleted(data)) {
      model_.remove(*id);
      swallowRelease_.remove(*id);
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray icon removal failed:" << error.what();
  }
}

void Systray::sweep() {
  if (!api_) return;
  try {
    for (const int id : registry_.sweepStale(*api_)) {
      model_.remove(id);
      swallowRelease_.remove(id);
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray sweep failed:" << error.what();
  }
}

void Systray::refresh() {
  try {
    if (api_) api_->broadcastTaskbarCreated();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray refresh failed:" << error.what();
  }
  sweep();
}

void Systray::click(int id, int button) {
  if (!api_) return;
  try {
    const platform::TrayIconData* icon = registry_.data(id);
    if (!icon) return;
    std::uint32_t down = 0;
    std::uint32_t up = 0;
    switch (button) {
      case Qt::LeftButton:
        if (swallowRelease_.remove(id)) return;
        down = platform::kWmLButtonDown;
        up = platform::kWmLButtonUp;
        break;
      case Qt::RightButton:
        down = platform::kWmRButtonDown;
        up = platform::kWmRButtonUp;
        break;
      case Qt::MiddleButton:
        down = platform::kWmMButtonDown;
        up = platform::kWmMButtonUp;
        break;
      default:
        return;
    }
    platform::sendTrayAction(*icon, *api_, down);
    platform::sendTrayAction(*icon, *api_, up);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray click failed:" << error.what();
  }
}

void Systray::doubleClick(int id) {
  if (!api_) return;
  try {
    const platform::TrayIconData* icon = registry_.data(id);
    if (!icon) return;
    swallowRelease_.insert(id);
    platform::sendTrayAction(*icon, *api_, platform::kWmLButtonDblClk);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray double click failed:" << error.what();
  }
}

}  // namespace tidy::services

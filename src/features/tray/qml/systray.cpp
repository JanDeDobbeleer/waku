#include "features/tray/qml/systray.h"

#include <QCoreApplication>
#include <QDebug>
#include <QQmlEngine>
#include <utility>

#include "features/tray/icon_bits.h"
#include "features/tray/qml/tray_image_provider.h"
#include "features/tray/tray_click.h"
#include "features/tray/tray_service.h"
#include "features/tray/tray_types.h"
#include "kernel/qt/live_desktop.h"

namespace waku::tray {
namespace {

constexpr int kRefreshDelayMs = 200;     // SystrayWidget's refreshTimer_
constexpr int kSweepIntervalMs = 30000;  // SystrayWidget checked every 5 s

struct RealHost {
  std::shared_ptr<TrayHost> host;
  QString reason;
};

// The window host lives as long as the process: a reload deletes the Systray and makes a new one,
// and every program would have to send its icons again if the host went with it. It is stopped
// when the application quits, which hands the tray back to Explorer.
std::shared_ptr<TrayHost>& sharedWindowHost() {
  static std::shared_ptr<TrayHost> host;
  return host;
}

// The host `--tray-host` chose (the hook, or the window host) or nothing.
RealHost makeRealHost() {
  RealHost result;
  if (kernel::capabilityDisabled(QStringLiteral("tray"))) {
    result.reason = QStringLiteral("tray unavailable: disabled by --no-tray");
    return result;
  }
  if (!kernel::liveDesktop()) {
    result.reason = QStringLiteral("tray unavailable: offscreen platform");
    return result;
  }
  waku::tray::TrayHostFactories factories = waku::tray::realTrayHostFactories();
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
  const TrayHostKind kind = trayHostKind();
  if (kind == TrayHostKind::Window && sharedWindowHost()) {
    result.host = sharedWindowHost();
    return result;
  }
  try {
    result.host =
        waku::tray::createTrayHost(kind, QCoreApplication::applicationDirPath(), factories);
    if (kind == TrayHostKind::Window && result.host) {
      sharedWindowHost() = result.host;
      QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                       QCoreApplication::instance(), [] {
                         if (auto host = std::exchange(sharedWindowHost(), nullptr)) host->stop();
                       });
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray host failed:" << error.what();
  }
  if (!result.host) {
    if (kind == TrayHostKind::Window) {
      result.reason = QStringLiteral("tray unavailable: another tray window host is running");
    } else if (!dllReason.isEmpty()) {
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
  return {kTrayBatteryIconGuid, kTrayVolumeIconGuid, kTrayNetworkIconGuid};
}

}  // namespace

Systray::Systray(QObject* parent) : QObject(parent) {
  RealHost real = makeRealHost();
  host_ = std::move(real.host);
  reason_ = std::move(real.reason);
  api_ = makeTrayWindowApi();
  registry_.setFilter(systemGuids(), {});
  connectHost();
}

Systray::Systray(std::unique_ptr<TrayHost> host, std::unique_ptr<TrayWindowApi> api,
                 QString unavailableReason, QObject* parent)
    : QObject(parent),
      host_(std::move(host)),
      api_(std::move(api)),
      reason_(std::move(unavailableReason)) {
  registry_.setFilter(systemGuids(), {});
  connectHost();
}

Systray::~Systray() {
  // The shared window host goes on without this widget; any other host is ours to stop.
  if (host_ && host_.use_count() == 1) host_->stop();
}

void Systray::connectHost() {
  if (!host_) return;
  // The host emits from its worker thread; the automatic connection queues onto this thread.
  connect(host_.get(), &TrayHost::iconModified, this,
          [this](const TrayIconData& data) { onIconModified(data); });
  connect(host_.get(), &TrayHost::iconDeleted, this,
          [this](const TrayIconData& data) { onIconDeleted(data); });
  // The hook has just connected: have Explorer announce every icon it already has.
  refreshTimer_.setSingleShot(true);
  refreshTimer_.setInterval(kRefreshDelayMs);
  connect(host_.get(), &TrayHost::updateIcons, this, [this] { refreshTimer_.start(); });
  connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
  connect(&sweepTimer_, &QTimer::timeout, this, [this] { sweep(); });
  host_->start();
  // A host that was already running (the window host after a reload) knows the icons.
  host_->replay();
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

void Systray::setFilter(bool showSystemIcons, const QStringList& hiddenExeNames) {
  registry_.setFilter(showSystemIcons ? QSet<QUuid>() : systemGuids(), hiddenExeNames);
}

void Systray::onIconModified(const TrayIconData& data) {
  try {
    const auto result = registry_.modified(data);
    if (result.change == waku::tray::TrayIconRegistry::Change::Ignored) return;
    if (const TrayIconData* stored = registry_.data(result.id)) {
      model_.apply(result.id, *stored);
      diagnoseIcon(result.id, data, *stored);
      updateSweepTimer();
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray icon update failed:" << error.what();
  }
}

void Systray::diagnoseIcon(int id, const TrayIconData& incoming, const TrayIconData& stored) {
  QString reason;
  if ((incoming.uFlags & kNifIcon) && incoming.iconRgba.isEmpty()) {
    reason = incoming.iconError.isEmpty() ? QStringLiteral("the icon could not be read")
                                          : incoming.iconError;
    if (!stored.iconRgba.isEmpty()) reason += QStringLiteral(", keeping the previous image");
  } else if (stored.iconRgba.isEmpty()) {
    reason = QStringLiteral("no icon received yet");
  } else if (const auto problem = waku::tray::trayIconPixelsProblem(stored)) {
    reason = *problem;
  }
  if (reason.isEmpty()) return;
  QSet<QString>& logged = loggedIconProblems_[id];
  if (logged.contains(reason)) return;
  logged.insert(reason);
  // Only the program's file name and the run-local id: no tooltip, path or window handle.
  const QString name =
      stored.exe.isEmpty() ? QString::number(id) : QStringLiteral("%1#%2").arg(stored.exe).arg(id);
  qWarning().noquote() << QStringLiteral("waku: tray icon %1: %2").arg(name, reason);
}

void Systray::onIconDeleted(const TrayIconData& data) {
  try {
    if (const auto id = registry_.deleted(data)) {
      model_.remove(*id);
      swallowRelease_.remove(*id);
      loggedIconProblems_.remove(*id);
    }
    updateSweepTimer();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray icon removal failed:" << error.what();
  }
}

// The sweep only has something to check while the registry holds an icon.
void Systray::updateSweepTimer() {
  const bool needed = !registry_.ids().empty();
  if (needed && !sweepTimer_.isActive()) {
    sweepTimer_.start(kSweepIntervalMs);
  } else if (!needed && sweepTimer_.isActive()) {
    sweepTimer_.stop();
  }
}

void Systray::sweep() {
  if (!api_) return;
  try {
    for (const int id : registry_.sweepStale(*api_)) {
      model_.remove(id);
      swallowRelease_.remove(id);
      loggedIconProblems_.remove(id);
    }
    updateSweepTimer();
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
    const TrayIconData* icon = registry_.data(id);
    if (!icon) return;
    std::uint32_t down = 0;
    std::uint32_t up = 0;
    switch (button) {
      case Qt::LeftButton:
        if (swallowRelease_.remove(id)) return;
        down = kWmLButtonDown;
        up = kWmLButtonUp;
        break;
      case Qt::RightButton:
        down = kWmRButtonDown;
        up = kWmRButtonUp;
        break;
      case Qt::MiddleButton:
        down = kWmMButtonDown;
        up = kWmMButtonUp;
        break;
      default:
        return;
    }
    sendTrayAction(*icon, *api_, down);
    sendTrayAction(*icon, *api_, up);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray click failed:" << error.what();
  }
}

void Systray::doubleClick(int id) {
  if (!api_) return;
  try {
    const TrayIconData* icon = registry_.data(id);
    if (!icon) return;
    swallowRelease_.insert(id);
    sendTrayAction(*icon, *api_, kWmLButtonDblClk);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray double click failed:" << error.what();
  }
}

}  // namespace waku::tray

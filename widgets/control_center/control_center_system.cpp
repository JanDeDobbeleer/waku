#include "widgets/control_center/control_center_system.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <algorithm>

#include "core/widgets/polling_worker_registry.h"
#include "platform/dnd.h"
#include "platform/shell_open.h"
#include "platform/theme.h"
#include "platform/touch_keyboard.h"
#include "platform/user_info.h"

namespace yasb::widgets {
namespace {

bool hasAction(const config::options::ControlCenterConfig& options, const char* id) {
  const auto& actions = options.sections.quick_actions.actions;
  return std::any_of(actions.begin(), actions.end(),
                     [id](const auto& action) { return action.id == id; });
}

// PowerOperations._connect_about_to_quit + exit_application: the command runs once the event loop
// has ended, so the bar is gone before the machine restarts or signs out.
void runAfterQuit(const QStringList& arguments) {
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp,
                   [arguments] { platform::runShutdown(arguments); });
  QCoreApplication::quit();
}

}  // namespace

ControlCenterSystem makeSystemControlCenter(PollingWorkerRegistry& registry,
                                            const config::options::ControlCenterConfig& options) {
  ControlCenterSystem system;
  const auto& sliders = options.sections.sliders;

  if ((sliders.show && sliders.volume.show_slider) || hasAction(options, "toggle_mute")) {
    system.output = registry.service<platform::AudioEndpoints>(
        "audio-output", [] { return platform::createSystemAudio(platform::AudioFlow::Render); });
  }
  if ((sliders.show && sliders.microphone.show_slider) || hasAction(options, "toggle_mic_mute")) {
    system.input = registry.service<platform::AudioEndpoints>(
        "audio-input", [] { return platform::createSystemAudio(platform::AudioFlow::Capture); });
  }
  if (sliders.show && sliders.brightness.show_slider) {
    system.brightness = registry.service<platform::BrightnessControl>(
        "brightness", [] { return platform::createSystemBrightness(); });
  }
  if (options.sections.media.show) {
    // The same shared session service the media widget uses (key "media").
    system.media = registry.service<platform::MediaSessionService>("media", [] {
      auto* created = new platform::MediaSessionService(platform::createSystemMediaSource());
      created->start();
      return created;
    });
  }

  system.dndStatus = [] { return platform::dndStatus(); };
  system.setDndStatus = [](const QString& mode) { platform::setDndStatus(mode); };
  system.themeMode = [] { return platform::themeMode(); };
  system.toggleTheme = [] { return platform::toggleThemeMode(); };
  system.toggleTouchKeyboard = [] { platform::toggleTouchKeyboard(); };
  system.startScreenshot = [] { platform::shellOpen(QStringLiteral("ms-screenclip:")); };
  system.openLink = [](const QString& uri) { platform::shellOpen(uri); };
  system.runCommand = [](const QString& file, const QString& parameters) {
    platform::shellOpen(file, parameters);
  };
  system.beep = [] { QApplication::beep(); };

  system.lock = [] { platform::lockWorkstation(); };
  system.signOut = [] { runAfterQuit({QStringLiteral("/l")}); };
  system.sleep = [] { platform::suspendSystem(); };
  system.hibernate = [] { platform::hibernateSystem(); };
  system.restart = [] {
    runAfterQuit({QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0")});
  };
  system.shutdown = [] {
    runAfterQuit({QStringLiteral("/s"), QStringLiteral("/hybrid"), QStringLiteral("/t"),
                  QStringLiteral("0")});
  };

  system.powerPlans = [](std::optional<QUuid>* active) { return platform::powerPlans(active); };
  system.setPowerPlan = [](const QUuid& guid) { return platform::setPowerPlan(guid); };
  system.powerModes = [] { return platform::powerModes(); };
  system.powerModeSupported = [] { return platform::powerModeSupported(); };
  system.activePowerMode = [] { return platform::activePowerMode(); };
  system.setPowerMode = [](const QUuid& guid) { return platform::setPowerMode(guid); };
  system.windows10 = platform::isWindows10();

  system.userName = [] { return platform::windowsUsername(); };
  system.avatarPath = [] { return platform::userAvatarPath(); };
  system.monitorOf = [](QWidget* widget) {
    return widget ? platform::monitorForWindow(widget->window()->winId()) : qulonglong(0);
  };
  return system;
}

}  // namespace yasb::widgets

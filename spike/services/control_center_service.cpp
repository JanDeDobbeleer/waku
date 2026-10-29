#include "spike/services/control_center_service.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>

#include "platform/dnd.h"
#include "platform/shell_open.h"
#include "platform/theme.h"
#include "platform/touch_keyboard.h"
#include "platform/user_info.h"

namespace wabar::services {

namespace {

bool isOffscreen() { return QGuiApplication::platformName() == QStringLiteral("offscreen"); }

// PowerOperations._connect_about_to_quit: the command runs once the event loop has ended, so the
// bar is gone before the machine restarts or signs out.
void runAfterQuit(const QStringList& arguments) {
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp,
                   [arguments] { platform::runShutdown(arguments); });
  QCoreApplication::quit();
}

ControlCenter::Backend systemBackend() {
  ControlCenter::Backend b;
  b.dndStatus = [] { return platform::dndStatus(); };
  b.setDndStatus = [](const QString& mode) { return platform::setDndStatus(mode); };
  b.themeMode = [] { return platform::themeMode(); };
  b.toggleTheme = [] { return platform::toggleThemeMode(); };
  b.toggleTouchKeyboard = [] { platform::toggleTouchKeyboard(); };
  b.powerPlans = [](std::optional<QUuid>* active) { return platform::powerPlans(active); };
  b.setPowerPlan = [](const QUuid& guid) { return platform::setPowerPlan(guid); };
  b.powerModeSupported = [] { return !platform::isWindows10() && platform::powerModeSupported(); };
  b.powerModes = [] { return platform::powerModes(); };
  b.activePowerMode = [] { return platform::activePowerMode(); };
  b.setPowerMode = [](const QUuid& guid) { return platform::setPowerMode(guid); };
  b.userName = [] { return platform::windowsUsername(); };
  b.avatarPath = [] { return platform::userAvatarPath(); };
  return b;
}

ControlCenter::Launcher systemLauncher() {
  ControlCenter::Launcher l;
  l.lock = [] { platform::lockWorkstation(); };
  l.sleep = [] { platform::suspendSystem(); };
  l.hibernate = [] { platform::hibernateSystem(); };
  l.signOut = [] { runAfterQuit({QStringLiteral("/l")}); };
  l.restart = [] {
    runAfterQuit({QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0")});
  };
  l.shutdown = [] {
    runAfterQuit({QStringLiteral("/s"), QStringLiteral("/hybrid"), QStringLiteral("/t"),
                  QStringLiteral("0")});
  };
  l.open = [](const QString& uri) { platform::shellOpen(uri); };
  return l;
}

QString idString(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }

}  // namespace

ControlCenter::ControlCenter(QObject* parent)
    : ControlCenter(isOffscreen() ? Backend{} : systemBackend(),
                    isOffscreen() ? Launcher{} : systemLauncher(),
                    isOffscreen() ? nullptr : platform::createSystemBrightness(nullptr), parent) {
  available_ = !isOffscreen();
  if (!available_) return;
  // Focus Assist changes made outside the bar (Settings, the action centre).
  auto* watcher = new platform::DndWatcher(this);
  connect(watcher, &platform::DndWatcher::statusChanged, this, &ControlCenter::onDndStatus);
  watcher->start();
}

ControlCenter::ControlCenter(Backend backend, Launcher launcher,
                             platform::BrightnessControl* brightness, QObject* parent)
    : QObject(parent),
      backend_(std::move(backend)),
      launcher_(std::move(launcher)),
      brightnessControl_(brightness) {
  pendingTimer_.setSingleShot(true);
  pendingTimer_.setInterval(kConfirmMs);
  connect(&pendingTimer_, &QTimer::timeout, this, &ControlCenter::clearPending);
  if (brightnessControl_) {
    brightnessControl_->setParent(this);
    // A slider write, a power-setting notification or a DDC re-read.
    connect(brightnessControl_, &platform::BrightnessControl::brightnessChanged, this,
            [this](qulonglong, int) {
              readBrightness();
              emit changed();
            });
  }
  init();
}

ControlCenter::~ControlCenter() {
  if (brightnessControl_) brightnessControl_->stop();
}

void ControlCenter::init() {
  if (backend_.userName) userName_ = backend_.userName();
  if (backend_.avatarPath) avatarPath_ = backend_.avatarPath();
  if (backend_.dndStatus) dnd_ = backend_.dndStatus();
  if (backend_.themeMode) theme_ = backend_.themeMode();
  readBrightness();
  readPower();
}

void ControlCenter::refresh() {
  if (backend_.dndStatus) dnd_ = backend_.dndStatus();
  if (backend_.themeMode) theme_ = backend_.themeMode();
  if (brightnessControl_) brightnessControl_->refreshNow();
  readBrightness();
  readPower();
  emit changed();
}

void ControlCenter::readBrightness() {
  brightnessMonitor_.reset();
  if (!brightnessControl_) return;
  const auto monitors = brightnessControl_->monitors();
  if (monitors.empty()) return;
  const auto kept = std::find_if(monitors.begin(), monitors.end(), [this](const auto& m) {
    return selectedMonitor_ && m.id == *selectedMonitor_;
  });
  brightnessMonitor_ = kept != monitors.end() ? kept->id : monitors.front().id;
  brightness_ = brightnessControl_->brightness(*brightnessMonitor_).value_or(brightness_);
}

void ControlCenter::readPower() {
  plans_.clear();
  activePlan_.reset();
  if (backend_.powerPlans) plans_ = backend_.powerPlans(&activePlan_);
  modeSupported_ = backend_.powerModeSupported && backend_.powerModeSupported();
  modes_.clear();
  modeName_.clear();
  if (!modeSupported_) return;
  if (backend_.powerModes) modes_ = backend_.powerModes();
  if (backend_.activePowerMode) {
    if (const auto mode = backend_.activePowerMode()) modeName_ = mode->name;
  }
}

QVariantList ControlCenter::brightnessSources() const {
  QVariantList list;
  if (!brightnessControl_) return list;
  int index = 0;
  for (const auto& monitor : brightnessControl_->monitors()) {
    list << QVariantMap{
        {QStringLiteral("id"), QString::number(monitor.id)},
        {QStringLiteral("name"), brightnessControl_->monitorSubtitle(monitor.id, index++)}};
  }
  return list;
}

QString ControlCenter::brightnessSource() const {
  return brightnessMonitor_ ? QString::number(*brightnessMonitor_) : QString();
}

QVariantList ControlCenter::powerPlans() const {
  QVariantList list;
  for (const auto& plan : plans_) {
    list << QVariantMap{{QStringLiteral("id"), idString(plan.guid)},
                        {QStringLiteral("name"), plan.name},
                        {QStringLiteral("active"), activePlan_ && *activePlan_ == plan.guid}};
  }
  return list;
}

QString ControlCenter::powerPlanName() const {
  for (const auto& plan : plans_) {
    if (activePlan_ && *activePlan_ == plan.guid) return plan.name;
  }
  return {};
}

QVariantList ControlCenter::powerModes() const {
  QVariantList list;
  for (const auto& mode : modes_) {
    list << QVariantMap{{QStringLiteral("id"), idString(mode.guid)},
                        {QStringLiteral("name"), mode.name},
                        {QStringLiteral("active"), mode.name == modeName_}};
  }
  return list;
}

QString ControlCenter::avatarUrl() const {
  return avatarPath_.isEmpty() ? QString() : QUrl::fromLocalFile(avatarPath_).toString();
}

void ControlCenter::onDndStatus(const QString& status) {
  if (dnd_ == status) return;
  dnd_ = status;
  emit changed();
}

void ControlCenter::toggleDnd() {
  if (!backend_.dndStatus || !backend_.setDndStatus) return;
  const QString next = backend_.dndStatus() != QLatin1String("disabled")
                           ? QStringLiteral("disabled")
                           : QStringLiteral("priority");
  if (backend_.setDndStatus(next)) dnd_ = next;
  emit changed();
}

void ControlCenter::cycleDnd() {
  if (!backend_.dndStatus || !backend_.setDndStatus) return;
  const QString current = backend_.dndStatus();
  const QString next = current == QLatin1String("disabled")   ? QStringLiteral("priority")
                       : current == QLatin1String("priority") ? QStringLiteral("alarms")
                                                              : QStringLiteral("disabled");
  if (backend_.setDndStatus(next)) dnd_ = next;
  emit changed();
}

void ControlCenter::toggleTheme() {
  if (!backend_.toggleTheme) return;
  if (const auto mode = backend_.toggleTheme()) theme_ = *mode;
  emit changed();
}

void ControlCenter::toggleTouchKeyboard() {
  if (backend_.toggleTouchKeyboard) backend_.toggleTouchKeyboard();
}

void ControlCenter::screenshot() {
  // The system snipping overlay, as the Widgets port does until an editor exists.
  if (launcher_.open) launcher_.open(QStringLiteral("ms-screenclip:"));
}

void ControlCenter::openSettings(const QString& uri) {
  // Only Settings pages: this is not a general launcher.
  if (!uri.startsWith(QLatin1String("ms-settings:"))) return;
  if (launcher_.open) launcher_.open(uri);
}

void ControlCenter::setBrightness(int percent) {
  if (!brightnessControl_ || !brightnessMonitor_) return;
  brightness_ = std::clamp(percent, 0, 100);
  brightnessControl_->setBrightness(*brightnessMonitor_, brightness_);
  emit changed();
}

void ControlCenter::setBrightnessSource(const QString& id) {
  bool ok = false;
  const qulonglong value = id.toULongLong(&ok);
  if (!ok || !brightnessControl_) return;
  selectedMonitor_ = value;
  readBrightness();
  emit changed();
}

bool ControlCenter::setPowerPlan(const QString& id) {
  const QUuid guid(id);
  const auto plan = std::find_if(plans_.begin(), plans_.end(),
                                 [&](const platform::PowerPlan& p) { return p.guid == guid; });
  if (plan == plans_.end() || !backend_.setPowerPlan) return false;
  if (activePlan_ && *activePlan_ == guid) return true;
  const bool ok = backend_.setPowerPlan(guid) == 0;
  // A plan change can change which modes exist; always re-read.
  readPower();
  emit changed();
  return ok;
}

bool ControlCenter::setPowerMode(const QString& id) {
  const QUuid guid(id);
  const auto mode = std::find_if(modes_.begin(), modes_.end(),
                                 [&](const platform::PowerMode& m) { return m.guid == guid; });
  if (mode == modes_.end() || !backend_.setPowerMode) return false;
  const bool ok = backend_.setPowerMode(guid);
  readPower();
  emit changed();
  return ok;
}

void ControlCenter::clearPending() {
  pendingTimer_.stop();
  if (pending_.isEmpty()) return;
  pending_.clear();
  emit changed();
}

void ControlCenter::cancelAction() { clearPending(); }

bool ControlCenter::requestAction(const QString& id) {
  struct Entry {
    const char* id;
    const std::function<void()>* run;
    bool confirm;
  };
  const Entry table[] = {
      {"lock", &launcher_.lock, false},           {"sleep", &launcher_.sleep, false},
      {"hibernate", &launcher_.hibernate, false}, {"signOut", &launcher_.signOut, true},
      {"restart", &launcher_.restart, true},      {"shutdown", &launcher_.shutdown, true},
  };
  for (const Entry& entry : table) {
    if (id != QLatin1String(entry.id)) continue;
    if (!*entry.run) return false;
    if (entry.confirm && pending_ != id) {
      pending_ = id;
      pendingTimer_.start();
      emit changed();
      return false;
    }
    clearPending();
    (*entry.run)();
    return true;
  }
  return false;
}

}  // namespace wabar::services

#include "features/control_center/qml/control_center_service.h"

#include <QCoreApplication>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>

#include "features/control_center/app_theme.h"
#include "features/control_center/touch_keyboard.h"
#include "features/control_center/user_info.h"
#include "features/notifications/dnd.h"
#include "kernel/qt/gui_post.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/shell_launch.h"

namespace waku::control_center {

namespace {

// PowerOperations._connect_about_to_quit: the command runs once the event loop has ended, so the
// bar is gone before the machine restarts or signs out.
void runAfterQuit(const QStringList& arguments) {
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp,
                   [arguments] { runShutdown(arguments); });
  QCoreApplication::quit();
}

ControlCenter::Backend systemBackend() {
  ControlCenter::Backend b;
  b.dndStatus = [] { return waku::notifications::dndStatus(); };
  b.setDndStatus = [](const QString& mode) { return waku::notifications::setDndStatus(mode); };
  b.themeMode = [] { return themeMode(); };
  b.toggleTheme = [] { return toggleThemeMode(); };
  b.toggleTouchKeyboard = [] { toggleTouchKeyboard(); };
  b.powerPlans = [](std::optional<QUuid>* active) { return powerPlans(active); };
  b.setPowerPlan = [](const QUuid& guid) { return setPowerPlan(guid); };
  b.powerModeSupported = [] { return !isWindows10() && powerModeSupported(); };
  b.powerModes = [] { return powerModes(); };
  b.activePowerMode = [] { return activePowerMode(); };
  b.setPowerMode = [](const QUuid& guid) { return setPowerMode(guid); };
  b.userName = [] { return windowsUsername(); };
  b.avatarPath = [] { return userAvatarPath(); };
  return b;
}

ControlCenter::Launcher systemLauncher() {
  ControlCenter::Launcher l;
  l.lock = [] { lockWorkstation(); };
  l.sleep = [] { suspendSystem(); };
  l.hibernate = [] { hibernateSystem(); };
  l.signOut = [] { runAfterQuit({QStringLiteral("/l")}); };
  l.restart = [] {
    runAfterQuit({QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0")});
  };
  l.shutdown = [] {
    runAfterQuit({QStringLiteral("/s"), QStringLiteral("/hybrid"), QStringLiteral("/t"),
                  QStringLiteral("0")});
  };
  l.open = [](const QString& uri) { kernel::shellOpen(uri); };
  return l;
}

QString idString(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }

}  // namespace

ControlCenter::ControlCenter(QObject* parent)
    : ControlCenter(
          kernel::liveDesktop() ? systemBackend() : Backend{},
          kernel::liveDesktop() ? systemLauncher() : Launcher{},
          kernel::liveDesktop() ? waku::brightness::createSystemBrightness(nullptr) : nullptr,
          parent, /*readLater=*/kernel::liveDesktop()) {
  available_ = kernel::liveDesktop();
  if (!available_) return;
  // Focus Assist changes made outside the bar (Settings, the action centre).
  auto* watcher = new waku::notifications::DndWatcher(this);
  connect(watcher, &waku::notifications::DndWatcher::statusChanged, this,
          &ControlCenter::onDndStatus);
  watcher->start();
}

ControlCenter::ControlCenter(Backend backend, Launcher launcher,
                             waku::brightness::BrightnessControl* brightness, QObject* parent)
    : ControlCenter(std::move(backend), std::move(launcher), brightness, parent, false) {}

ControlCenter::ControlCenter(Backend backend, Launcher launcher,
                             waku::brightness::BrightnessControl* brightness, QObject* parent,
                             bool readLater)
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
    connect(brightnessControl_, &waku::brightness::BrightnessControl::brightnessChanged, this,
            [this](qulonglong, int) { announce(kBrightness | readBrightness()); });
    connect(brightnessControl_, &waku::brightness::BrightnessControl::contrastChanged, this,
            [this](qulonglong id, int value) {
              if (!brightnessMonitor_ || *brightnessMonitor_ != id) return;
              contrast_ = value;
              announce(kContrast);
            });
  }
  if (readLater) {
    readBrightness();
    initLater();
  } else {
    init();
  }
}

ControlCenter::~ControlCenter() {
  if (brightnessControl_) brightnessControl_->stop();
}

void ControlCenter::announce(unsigned groups) {
  if (groups & kDnd) emit dndChanged();
  if (groups & kTheme) emit themeChanged();
  if (groups & kBrightness) emit brightnessChanged();
  if (groups & kContrast) emit contrastChanged();
  if (groups & kBrightnessSources) emit brightnessSourcesChanged();
  if (groups & kPower) emit powerChanged();
  if (groups & kUser) emit userChanged();
  if (groups & kPending) emit pendingActionChanged();
  emit changed();
}

void ControlCenter::init() {
  if (backend_.userName) userName_ = backend_.userName();
  if (backend_.avatarPath) avatarPath_ = backend_.avatarPath();
  if (backend_.dndStatus) dnd_ = backend_.dndStatus();
  if (backend_.themeMode) theme_ = backend_.themeMode();
  readBrightness();
  readPower();
}

namespace {

// What initLater() reads off the GUI thread.
struct InitialState {
  QString userName;
  QString avatarPath;
  std::optional<QString> dnd;
  std::optional<QString> theme;
  std::vector<PowerPlan> plans;
  std::optional<QUuid> activePlan;
  bool modeSupported = false;
  std::vector<PowerMode> modes;
  QString modeName;
};

}  // namespace

// The user's display name (GetUserNameExW, hundreds of ms cold), the account picture (a registry
// walk), DND (an out-of-process COM call), the theme and the power plans and modes were read in
// the constructor, on the GUI thread, inside the first build and every reload. The backend's
// functions do not touch this object; the result goes back through qApp and is dropped when
// this object is gone.
void ControlCenter::initLater() {
  QPointer<ControlCenter> self(this);
  QThreadPool::globalInstance()->start([self, backend = backend_] {
    auto state = std::make_shared<InitialState>();
    try {
      if (backend.userName) state->userName = backend.userName();
      if (backend.avatarPath) state->avatarPath = backend.avatarPath();
      if (backend.dndStatus) state->dnd = backend.dndStatus();
      if (backend.themeMode) state->theme = backend.themeMode();
      if (backend.powerPlans) state->plans = backend.powerPlans(&state->activePlan);
      state->modeSupported = backend.powerModeSupported && backend.powerModeSupported();
      if (state->modeSupported) {
        if (backend.powerModes) state->modes = backend.powerModes();
        if (backend.activePowerMode) {
          if (const auto mode = backend.activePowerMode()) state->modeName = mode->name;
        }
      }
    } catch (...) {
    }
    kernel::postToGui(self, [self, state] {
      unsigned groups = kUser;
      self->userName_ = state->userName;
      self->avatarPath_ = state->avatarPath;
      if (state->dnd && !self->dndRead_) {
        self->dnd_ = *state->dnd;
        groups |= kDnd;
      }
      if (state->theme && !self->themeRead_) {
        self->theme_ = *state->theme;
        groups |= kTheme;
      }
      if (!self->powerRead_) {
        self->plans_ = state->plans;
        self->activePlan_ = state->activePlan;
        self->modeSupported_ = state->modeSupported;
        self->modes_ = state->modes;
        self->modeName_ = state->modeName;
        groups |= kPower;
      }
      self->announce(groups);
    });
  });
}

void ControlCenter::refresh() {
  if (backend_.dndStatus) dnd_ = backend_.dndStatus();
  if (backend_.themeMode) theme_ = backend_.themeMode();
  dndRead_ = themeRead_ = true;
  if (brightnessControl_) brightnessControl_->refreshNow();
  readBrightness();
  readPower();
  announce(kAll);
}

unsigned ControlCenter::readBrightness() {
  const auto previousMonitor = brightnessMonitor_;
  const int previousContrast = contrast_;
  std::vector<qulonglong> ids;
  brightnessMonitor_.reset();
  if (brightnessControl_) {
    const auto monitors = brightnessControl_->monitors();
    for (const auto& monitor : monitors) ids.push_back(monitor.id);
    if (!monitors.empty()) {
      const auto kept = std::find_if(monitors.begin(), monitors.end(), [this](const auto& m) {
        return selectedMonitor_ && m.id == *selectedMonitor_;
      });
      brightnessMonitor_ = kept != monitors.end() ? kept->id : monitors.front().id;
      brightness_ = brightnessControl_->brightness(*brightnessMonitor_).value_or(brightness_);
      contrast_ = brightnessControl_->contrast(*brightnessMonitor_).value_or(0);
    }
  }
  if (!brightnessMonitor_) contrast_ = 0;
  unsigned groups = 0;
  if (ids != monitorIds_) {
    monitorIds_ = std::move(ids);
    groups |= kBrightnessSources;
  }
  if (brightnessMonitor_ != previousMonitor || contrast_ != previousContrast) groups |= kContrast;
  return groups;
}

void ControlCenter::readPower() {
  powerRead_ = true;
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
  dndRead_ = true;
  announce(kDnd);
}

// The DND state is current: the watcher mirrors changes made outside the bar. Only a state that
// has not been read yet asks the backend (an out-of-process COM call).
QString ControlCenter::currentDnd() const {
  return dnd_.isEmpty() && backend_.dndStatus ? backend_.dndStatus() : dnd_;
}

void ControlCenter::toggleDnd() {
  if (!backend_.dndStatus || !backend_.setDndStatus) return;
  const QString next = currentDnd() != QLatin1String("disabled") ? QStringLiteral("disabled")
                                                                 : QStringLiteral("priority");
  if (backend_.setDndStatus(next)) dnd_ = next;
  dndRead_ = true;
  announce(kDnd);
}

void ControlCenter::cycleDnd() {
  if (!backend_.dndStatus || !backend_.setDndStatus) return;
  const QString current = currentDnd();
  const QString next = current == QLatin1String("disabled")   ? QStringLiteral("priority")
                       : current == QLatin1String("priority") ? QStringLiteral("alarms")
                                                              : QStringLiteral("disabled");
  if (backend_.setDndStatus(next)) dnd_ = next;
  dndRead_ = true;
  announce(kDnd);
}

void ControlCenter::toggleTheme() {
  if (!backend_.toggleTheme) return;
  if (const auto mode = backend_.toggleTheme()) theme_ = *mode;
  themeRead_ = true;
  announce(kTheme);
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
  announce(kBrightness);
}

void ControlCenter::setContrast(int percent) {
  if (!contrastAvailable()) return;
  contrast_ = std::clamp(percent, 0, 100);
  brightnessControl_->setContrast(*brightnessMonitor_, contrast_);
  announce(kContrast);
}

void ControlCenter::setBrightnessSource(const QString& id) {
  bool ok = false;
  const qulonglong value = id.toULongLong(&ok);
  if (!ok || !brightnessControl_) return;
  selectedMonitor_ = value;
  announce(kBrightness | kContrast | readBrightness());
}

bool ControlCenter::setPowerPlan(const QString& id) {
  const QUuid guid(id);
  const auto plan = std::find_if(plans_.begin(), plans_.end(),
                                 [&](const PowerPlan& p) { return p.guid == guid; });
  if (plan == plans_.end() || !backend_.setPowerPlan) return false;
  if (activePlan_ && *activePlan_ == guid) return true;
  const bool ok = backend_.setPowerPlan(guid) == 0;
  // A plan change can change which modes exist; always re-read.
  readPower();
  announce(kPower);
  return ok;
}

bool ControlCenter::setPowerMode(const QString& id) {
  const QUuid guid(id);
  const auto mode = std::find_if(modes_.begin(), modes_.end(),
                                 [&](const PowerMode& m) { return m.guid == guid; });
  if (mode == modes_.end() || !backend_.setPowerMode) return false;
  const bool ok = backend_.setPowerMode(guid);
  readPower();
  announce(kPower);
  return ok;
}

void ControlCenter::clearPending() {
  pendingTimer_.stop();
  if (pending_.isEmpty()) return;
  pending_.clear();
  announce(kPending);
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
      announce(kPending);
      return false;
    }
    clearPending();
    (*entry.run)();
    return true;
  }
  return false;
}

}  // namespace waku::control_center

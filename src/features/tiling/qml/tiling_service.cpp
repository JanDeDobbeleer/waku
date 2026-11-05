#include "features/tiling/qml/tiling_service.h"

#include <QDebug>
#include <QGuiApplication>
#include <QJSEngine>
#include <QQmlEngine>
#include <QScreen>
#include <algorithm>
#include <exception>

#include "features/tiling/layouts.h"
#include "features/tiling/tiling_rules.h"
#include "kernel/qt/live_desktop.h"

namespace waku::tiling {
namespace {

Tiling::Deps liveDeps() {
  Tiling::Deps deps;
  if (!kernel::liveDesktop(QStringLiteral("tiling"))) return deps;
  deps.placer = makeWin32WindowPlacer();
  deps.hooks = makeWin32TilingHooks();
  deps.scheduler = makeQtScheduler();
  deps.selfElevated = processIsElevated();
  deps.blocker = [] { return runningWindowManager(); };
  return deps;
}

constexpr int kBlockerCheckMs = 5000;
constexpr qint64 kBlockerCacheMs = 3000;

QString yesNo(bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); }

}  // namespace

Tiling::Tiling(QObject* parent) : Tiling(liveDeps(), parent) {}

Tiling::Tiling(Deps deps, QObject* parent) : QObject(parent) { init(std::move(deps)); }

Tiling::~Tiling() {
  // The windows stay where they are; only the hooks go.
  if (hooks_) hooks_->uninstall();
}

void Tiling::init(Deps deps) {
  placer_ = std::move(deps.placer);
  hooks_ = std::move(deps.hooks);
  scheduler_ = std::move(deps.scheduler);
  blocker_ = std::move(deps.blocker);
  settings_.selfElevated = deps.selfElevated;
  watchScreens_ = deps.watchScreens;
  blockerTimer_.setInterval(kBlockerCheckMs);
  connect(&blockerTimer_, &QTimer::timeout, this, &Tiling::reevaluate);
  if (placer_ && scheduler_) engine_ = std::make_unique<TilingEngine>(*placer_, *scheduler_);

  screenTimer_.setSingleShot(true);
  screenTimer_.setInterval(deps.screenDebounceMs);
  connect(&screenTimer_, &QTimer::timeout, this, [this] {
    if (active_ && engine_) engine_->retileAll();
  });
  if (watchScreens_ && qGuiApp) {
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
      watchScreen(screen);
      screensChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) { screensChanged(); });
    for (QScreen* screen : QGuiApplication::screens()) watchScreen(screen);
  }
}

Tiling* Tiling::create(QQmlEngine*, QJSEngine*) { return new Tiling(nullptr); }

void Tiling::watchScreen(QScreen* screen) {
  connect(screen, &QScreen::availableGeometryChanged, this, &Tiling::screensChanged);
  connect(screen, &QScreen::geometryChanged, this, &Tiling::screensChanged);
}

void Tiling::screensChanged() {
  if (active_) screenTimer_.start();
}

// --- coexistence

// A process listing is not free: the answer is kept for a few seconds, and reevaluate() (the timer,
// `enabled`) asks for a fresh one.
QString Tiling::blockedBy() const {
  if (!blocker_) return {};
  if (!blockedAge_.isValid() || blockedAge_.hasExpired(kBlockerCacheMs)) {
    blockedBy_ = blocker_();
    blockedAge_.start();
  }
  return blockedBy_;
}

void Tiling::reevaluate() {
  try {
    blockedAge_.invalidate();
    const bool want = enabled_ && engine_ && hooks_ && blockedBy().isEmpty();
    if (want == active_) return;
    if (want) {
      if (!hooks_->install([this](const WindowEvent& event) { onEvent(event); })) {
        qWarning() << "tiling: no window event hook could be installed";
        return;
      }
      pushSettings();
      engine_->start();
      active_ = true;
    } else {
      hooks_->uninstall();
      engine_->stop();
      active_ = false;
    }
    emit activeChanged();
    refreshCurrentLayout();
  } catch (const std::exception& e) {
    qWarning().noquote() << "tiling: reevaluate failed:" << e.what();
  }
}

void Tiling::onEvent(const WindowEvent& event) {
  try {
    if (engine_) engine_->handle(event);
    if (event.kind == EventKind::Foreground) refreshCurrentLayout();
  } catch (const std::exception& e) {
    qWarning().noquote() << "tiling: event failed:" << e.what();
  } catch (...) {
    qWarning() << "tiling: event failed";
  }
}

// --- settings

void Tiling::setEnabled(bool enabled) {
  if (enabled == enabled_) return;
  enabled_ = enabled;
  if (enabled_) {
    blockerTimer_.start();
  } else {
    blockerTimer_.stop();
  }
  emit enabledChanged();
  reevaluate();
}

void Tiling::pushSettings() {
  if (engine_) engine_->setSettings(settings_);
  refreshCurrentLayout();
}

void Tiling::refreshCurrentLayout() {
  const QString now = active_ && engine_ ? engine_->currentLayout() : QString();
  if (now == currentLayout_) return;
  currentLayout_ = now;
  emit currentLayoutChanged();
}

void Tiling::setLayoutName(const QString& name) {
  const QString layout = canonicalLayout(name);
  if (layout.isEmpty()) {
    qWarning().noquote() << "tiling: unknown layout" << name << "("
                         << layouts().join(QLatin1String(", ")) << ")";
    return;
  }
  if (layout == settings_.layout) return;
  settings_.layout = layout;
  pushSettings();
  emit layoutChanged();
}

void Tiling::setGap(int gap) {
  gap = std::clamp(gap, 0, 200);
  if (gap == settings_.gap) return;
  settings_.gap = gap;
  pushSettings();
  emit gapChanged();
}

void Tiling::setRatio(double ratio) {
  ratio = std::clamp(ratio, 0.1, 0.9);
  if (qFuzzyCompare(ratio, settings_.ratio)) return;
  settings_.ratio = ratio;
  pushSettings();
  emit ratioChanged();
}

void Tiling::setRules(const QVariantList& rules) {
  rules_ = rules;
  const ParsedRules parsed = parseRules(rules);
  for (const QString& error : parsed.errors) qWarning().noquote() << "tiling:" << error;
  settings_.rules = parsed.rules;
  pushSettings();
  emit rulesChanged();
}

QStringList Tiling::layouts() const { return layoutNames(); }

// --- commands

void Tiling::toggleFloat() {
  if (active_ && engine_) engine_->setForegroundFloating(std::nullopt);
}

void Tiling::retile() {
  if (active_ && engine_) engine_->retileAll();
}

bool Tiling::setLayout(const QString& name) {
  const bool ok = active_ && engine_ && engine_->setWorkspaceLayout(name);
  refreshCurrentLayout();
  return ok;
}

QString Tiling::cycleLayout(int step) {
  if (!active_ || !engine_) return {};
  const CommandResult result = engine_->cycleLayout(step);
  refreshCurrentLayout();
  return result.message;
}

void Tiling::focusNext() {
  if (active_ && engine_) engine_->focusStep(1);
}

void Tiling::focusPrevious() {
  if (active_ && engine_) engine_->focusStep(-1);
}

QString Tiling::notActive() const {
  if (!engine_) {
    return QStringLiteral(
        "error: the tiler is not available here (offscreen platform or --no-tiling)");
  }
  if (!enabled_) return QStringLiteral("error: tiling is off (set Tiling.enabled in the shell)");
  const QString blocker = blockedBy();
  if (!blocker.isEmpty()) {
    return QStringLiteral("error: tiling yields to %1 while it runs").arg(blocker);
  }
  return QStringLiteral("error: the tiler is not running");
}

QString Tiling::controlFloat(std::optional<bool> floating) {
  if (!active_ || !engine_) return notActive();
  switch (engine_->setForegroundFloating(floating)) {
    case FloatOutcome::NoWindow:
      return QStringLiteral("error: no focused window");
    case FloatOutcome::Floated:
      return QStringLiteral("ok: floating");
    case FloatOutcome::Tiled:
      return QStringLiteral("ok: tiled");
    case FloatOutcome::Unchanged:
      return QStringLiteral("ok: unchanged");
    case FloatOutcome::Ineligible:
      return QStringLiteral("error: this window cannot be tiled");
  }
  return QStringLiteral("error: unexpected");
}

QString Tiling::controlLayout(const QString& name) {
  if (!active_ || !engine_) return notActive();
  if (!engine_->setWorkspaceLayout(name)) {
    return QStringLiteral("error: unknown layout '%1' (%2)")
        .arg(name, layouts().join(QLatin1String(", ")));
  }
  refreshCurrentLayout();
  return QStringLiteral("ok: layout %1").arg(canonicalLayout(name));
}

QString Tiling::controlLayoutStep(int step) {
  const QString answer = run([step](TilingEngine& e) {
    CommandResult result = e.cycleLayout(step);
    result.message = QStringLiteral("layout %1").arg(result.message);
    return result;
  });
  refreshCurrentLayout();
  return answer;
}

QString Tiling::controlRetile() {
  if (!active_ || !engine_) return notActive();
  engine_->retileAll();
  return QStringLiteral("ok: retiling");
}

QString Tiling::controlFocus(int direction) {
  if (!active_ || !engine_) return notActive();
  return engine_->focusStep(direction) ? QStringLiteral("ok: focused")
                                       : QStringLiteral("error: no window to focus");
}

QString Tiling::run(const std::function<CommandResult(TilingEngine&)>& command) {
  if (!active_ || !engine_) return notActive();
  try {
    const CommandResult result = command(*engine_);
    return (result.ok ? QStringLiteral("ok: ") : QStringLiteral("error: ")) + result.message;
  } catch (const std::exception& e) {
    return QStringLiteral("error: %1").arg(QString::fromUtf8(e.what()));
  }
}

QString Tiling::controlFocusDirection(Direction direction) {
  return run([direction](TilingEngine& e) { return e.focusDirection(direction); });
}

QString Tiling::controlMove(Direction direction) {
  return run([direction](TilingEngine& e) { return e.moveDirection(direction); });
}

QString Tiling::controlStack(Direction direction) {
  return run([direction](TilingEngine& e) { return e.stackDirection(direction); });
}

QString Tiling::controlUnstack() {
  return run([](TilingEngine& e) { return e.unstack(); });
}

QString Tiling::controlCycleStack(int step) {
  return run([step](TilingEngine& e) { return e.cycleStack(step); });
}

QString Tiling::controlPromote() {
  return run([](TilingEngine& e) { return e.promote(); });
}

QString Tiling::controlResize(Axis axis, int sign) {
  return run([axis, sign](TilingEngine& e) { return e.resize(axis, sign); });
}

QString Tiling::controlFlip(Axis axis) {
  return run([axis](TilingEngine& e) { return e.flip(axis); });
}

QString Tiling::controlMonocle() {
  return run([](TilingEngine& e) { return e.monocle(); });
}

QString Tiling::controlPause() {
  return run([](TilingEngine& e) { return e.togglePause(); });
}

QString Tiling::controlClose() {
  return run([](TilingEngine& e) { return e.closeForeground(); });
}

QString Tiling::controlMinimize() {
  return run([](TilingEngine& e) { return e.minimizeForeground(); });
}

QString Tiling::controlStatus() {
  const TilingStatus status = engine_ ? engine_->status() : TilingStatus();
  return QStringLiteral(
             "ok: enabled=%1; active=%2; blocked=%3; layout=%4; gap=%5; ratio=%6; managed=%7; "
             "floating=%8; workspaces=%9; stacks=%10; clamped=%11; paused=%12; desktop=%13")
      .arg(yesNo(enabled_), yesNo(active_),
           blockedBy().isEmpty() ? QStringLiteral("no") : blockedBy(),
           status.layout.isEmpty() ? settings_.layout : status.layout,
           QString::number(settings_.gap), QString::number(settings_.ratio))
      .arg(status.managed)
      .arg(status.floating)
      .arg(status.workspaces)
      .arg(status.stacks)
      .arg(status.clamped)
      .arg(yesNo(status.paused))
      .arg(status.desktop.isEmpty() ? QStringLiteral("unknown") : status.desktop);
}

}  // namespace waku::tiling

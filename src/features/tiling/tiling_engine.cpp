#include "features/tiling/tiling_engine.h"

#include <QDebug>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <exception>

#include "features/tiling/layouts.h"
#include "features/tiling/tiling_rules.h"

namespace waku::tiling {
namespace {

// After a Cloaked event the engine waits this long to tell "the window was cloaked by a desktop
// switch" from "the window cloaked itself": the registry value that names the shown desktop may
// trail the event.
constexpr int kCloakSettleMs = 150;
// A window counts as moved by the user when any edge of its visible bounds is further than this
// from where the tiler left it (rounding and DPI jitter stay below it).
constexpr int kMovedTolerance = 2;

bool differs(const QRect& a, const QRect& b) {
  return std::abs(a.x() - b.x()) > kMovedTolerance || std::abs(a.y() - b.y()) > kMovedTolerance ||
         std::abs(a.width() - b.width()) > kMovedTolerance ||
         std::abs(a.height() - b.height()) > kMovedTolerance;
}

class QtScheduler final : public Scheduler {
 public:
  void after(int ms, std::function<void()> task) override {
    QTimer::singleShot(ms, [task = std::move(task)] { task(); });
  }
};

}  // namespace

std::unique_ptr<Scheduler> makeQtScheduler() { return std::make_unique<QtScheduler>(); }

TilingEngine::TilingEngine(WindowPlacer& placer, Scheduler& scheduler)
    : placer_(placer), scheduler_(scheduler) {}

TilingEngine::~TilingEngine() = default;

void TilingEngine::defer(int ms, std::function<void()> task) {
  std::weak_ptr<bool> weak = alive_;
  scheduler_.after(ms, [this, weak, task = std::move(task)] {
    if (weak.expired()) return;
    try {
      task();
    } catch (const std::exception& e) {
      qWarning().noquote() << "tiling: task failed:" << e.what();
    } catch (...) {
      qWarning() << "tiling: task failed";
    }
  });
}

void TilingEngine::setSettings(const TilingSettings& settings) {
  const bool layoutChanged = settings.layout != settings_.layout;
  const bool ratioChanged = settings.ratio != settings_.ratio;
  settings_ = settings;
  if (!running_) return;
  if (layoutChanged) model_.clearLayouts();
  if (ratioChanged) model_.clearRatios();
  retileAll();
}

void TilingEngine::start() {
  if (running_) return;
  running_ = true;
  currentDesktop_ = placer_.currentDesktop();
  for (const WindowId hwnd : placer_.candidates()) evaluate(hwnd, 0, false);
  retileAll();
}

void TilingEngine::stop() {
  running_ = false;
  paused_ = false;
  model_.clear();
  ignored_.clear();
  pending_.clear();
  moving_.clear();
  placed_.clear();
  clamped_.clear();
  wasTop_.clear();
  retiles_.clear();
  flushScheduled_ = false;
  currentDesktop_.clear();
  alive_ = std::make_shared<bool>(true);  // the old token expires with its deferred tasks
}

// --- events

void TilingEngine::handle(const WindowEvent& event) {
  if (!running_ || event.hwnd == 0) return;
  const WindowId hwnd = event.hwnd;
  switch (event.kind) {
    case EventKind::Show:
    case EventKind::MinimizeEnd:
      if (!model_.contains(hwnd)) scheduleEvaluate(hwnd, 0, 0, false);
      break;
    case EventKind::NameChange: {
      // Frequent (terminals and browsers rename their windows all the time): look again only at a
      // window that is new to the tiler or was rejected for want of a title.
      if (model_.contains(hwnd) || model_.isFloating(hwnd)) break;
      const auto rejected = ignored_.find(hwnd);
      if (rejected != ignored_.end() && !rejected->second) break;
      scheduleEvaluate(hwnd, 0, 0, false);
      break;
    }
    case EventKind::Uncloaked:
      refreshDesktop();
      if (const auto key = model_.keyOf(hwnd)) {
        requestRetile(*key);
      } else {
        scheduleEvaluate(hwnd, 0, 0, false);
      }
      break;
    case EventKind::Cloaked:
      if (model_.contains(hwnd)) defer(kCloakSettleMs, [this, hwnd] { onCloaked(hwnd); });
      break;
    case EventKind::Hide: {
      const std::optional<WindowFacts> facts = placer_.query(hwnd);
      if (facts && facts->visible) break;  // a hide of a child or a transient state
      forget(hwnd, false);
      break;
    }
    case EventKind::Destroy:
      forget(hwnd, true);
      ignored_.erase(hwnd);
      pending_.erase(hwnd);
      break;
    case EventKind::MinimizeStart:
      forget(hwnd, false);
      break;
    case EventKind::MoveSizeStart:
      onMoveSizeStart(hwnd);
      break;
    case EventKind::MoveSizeEnd:
      onMoveSizeEnd(hwnd);
      break;
    case EventKind::Foreground:
      refreshDesktop();
      // A window that is under the top of its stack (alt-tab, the taskbar) takes the top.
      if (model_.raiseInStack(hwnd)) {
        if (const auto key = model_.keyOf(hwnd)) requestRetile(*key);
      }
      break;
  }
}

void TilingEngine::onCloaked(WindowId hwnd) {
  if (!running_ || !model_.contains(hwnd)) return;
  refreshDesktop();
  const std::optional<WindowFacts> facts = placer_.query(hwnd);
  if (!facts) {
    forget(hwnd, true);
    return;
  }
  if (!facts->cloaked) return;
  // Cloaked because its desktop is not the shown one. A desktop switch cloaks every window of the
  // desktop it leaves: those keep their place in that desktop's stack. A window that was filed
  // under the shown desktop and now belongs to another was moved away (Task View, `wakuc desktop
  // move`): it leaves this stack and the others fill the gap.
  if (!facts->desktopId.isEmpty() && !currentDesktop_.isEmpty() &&
      facts->desktopId != currentDesktop_) {
    const std::optional<WorkspaceKey> key = model_.keyOf(hwnd);
    if (key && key->desktop == currentDesktop_) forget(hwnd, false);
    return;
  }
  forget(hwnd, false);
}

void TilingEngine::onMoveSizeStart(WindowId hwnd) {
  if (!model_.contains(hwnd)) return;
  moving_.insert(hwnd);
  // The bounds now are what the end is compared with: a clamped window counts as unmoved.
  if (const std::optional<WindowFacts> facts = placer_.query(hwnd)) placed_[hwnd] = facts->rect;
}

void TilingEngine::onMoveSizeEnd(WindowId hwnd) {
  if (moving_.erase(hwnd) == 0 || !model_.contains(hwnd)) return;
  if (paused_) return;  // nothing was placed, so there is nothing to compare with
  const std::optional<WindowFacts> facts = placer_.query(hwnd);
  const std::optional<WorkspaceKey> key = model_.keyOf(hwnd);
  if (!facts) {
    forget(hwnd, true);
    return;
  }
  const auto placed = placed_.find(hwnd);
  if (placed != placed_.end() && differs(placed->second, facts->rect)) {
    // The user took this window out of the layout.
    model_.setFloating(hwnd, true);
    placed_.erase(hwnd);
  }
  if (key) requestRetile(*key);  // the others fill the gap, or the window snaps back
}

// --- evaluation

void TilingEngine::scheduleEvaluate(WindowId hwnd, int ms, int attempt, bool force) {
  if (!pending_.insert(hwnd).second) return;
  defer(ms, [this, hwnd, attempt, force] { evaluate(hwnd, attempt, force); });
}

void TilingEngine::evaluate(WindowId hwnd, int attempt, bool force) {
  if (!running_) return;
  pending_.erase(hwnd);
  const std::optional<WindowFacts> facts = placer_.query(hwnd);
  if (!facts) {
    forget(hwnd, true);
    ignored_.erase(hwnd);
    return;
  }
  if (model_.contains(hwnd)) return;
  if (model_.isFloating(hwnd) && !force) return;

  const Eligibility eligibility = classify(*facts, settings_.rules, settings_.selfElevated, force);
  switch (eligibility.verdict) {
    case Verdict::Ignore:
      ignored_[hwnd] = false;
      break;
    case Verdict::Float:
      ignored_.erase(hwnd);
      model_.setFloating(hwnd, true);
      break;
    case Verdict::Retry:
      if (attempt < kMaxRetries) {
        scheduleEvaluate(hwnd, kRetryMs, attempt + 1, force);
      } else {
        ignored_[hwnd] = eligibility.reason == QLatin1String("untitled");
      }
      break;
    case Verdict::Manage: {
      ignored_.erase(hwnd);
      const WorkspaceKey key = keyFor(*facts);
      model_.add(key, hwnd);
      requestRetile(key);
      break;
    }
  }
}

void TilingEngine::forget(WindowId hwnd, bool dropFloat) {
  placed_.erase(hwnd);
  clamped_.erase(hwnd);
  moving_.erase(hwnd);
  wasTop_.erase(hwnd);
  const std::optional<WorkspaceKey> key = model_.remove(hwnd);
  if (dropFloat) model_.setFloating(hwnd, false);
  if (key) requestRetile(*key);
}

WorkspaceKey TilingEngine::keyFor(const WindowFacts& facts) const {
  return WorkspaceKey{facts.monitor, facts.desktopId.isEmpty() ? currentDesktop_ : facts.desktopId};
}

void TilingEngine::refreshDesktop() {
  const QString desktop = placer_.currentDesktop();
  if (desktop.isEmpty() || desktop == currentDesktop_) return;
  currentDesktop_ = desktop;
  for (const WorkspaceKey& key : model_.keys()) {
    if (key.desktop == desktop) requestRetile(key);
  }
}

// --- placement

void TilingEngine::retileAll() {
  if (!running_) return;
  for (const WorkspaceKey& key : model_.keys()) {
    if (currentDesktop_.isEmpty() || key.desktop == currentDesktop_) requestRetile(key);
  }
}

void TilingEngine::requestRetile(const WorkspaceKey& key) {
  retiles_.insert(key);
  if (flushScheduled_) return;
  flushScheduled_ = true;
  defer(0, [this] { flushRetiles(); });
}

void TilingEngine::flushRetiles() {
  flushScheduled_ = false;
  if (!running_) return;
  const std::set<WorkspaceKey> keys = std::move(retiles_);
  retiles_.clear();
  for (const WorkspaceKey& key : keys) retile(key);
}

void TilingEngine::retile(const WorkspaceKey& key) {
  if (!running_ || paused_) return;
  // Never place a window of a desktop that is not shown: it is cloaked.
  if (!currentDesktop_.isEmpty() && key.desktop != currentDesktop_) return;
  const std::vector<Slot> order =
      model_.slotList(key);  // a copy: forgetting a window edits the model
  if (order.empty()) return;
  const QRect area = placer_.workArea(key.monitor);
  if (area.isEmpty()) return;

  // Every slot gives one window (its top) to the layout. The windows under the top follow it to
  // the same rectangle.
  std::vector<WindowId> active;
  std::map<WindowId, QRect> current;
  std::map<WindowId, std::vector<WindowId>> beneath;  // top -> the windows under it
  for (const Slot& slot : order) {
    std::vector<WindowId> hidden;
    bool topActive = false;
    for (const WindowId hwnd : slot.windows) {
      const std::optional<WindowFacts> facts = placer_.query(hwnd);
      if (!facts) {
        forget(hwnd, true);
        continue;
      }
      if (!facts->visible) {
        forget(hwnd, false);
        continue;
      }
      if (facts->monitor != 0 && facts->monitor != key.monitor) {
        // Moved to another monitor by something other than a drag (a hotkey, a script).
        const WorkspaceKey moved{facts->monitor, key.desktop};
        model_.add(moved, hwnd);
        placed_.erase(hwnd);
        requestRetile(moved);
        requestRetile(key);
        continue;
      }
      // A minimized, maximized or cloaked window keeps its place in the stack but takes no slot.
      if (facts->iconic || facts->zoomed || facts->cloaked) continue;
      current[hwnd] = facts->rect;
      if (hwnd == slot.topWindow()) {
        active.push_back(hwnd);
        topActive = true;
      } else {
        hidden.push_back(hwnd);
      }
    }
    if (topActive) beneath[slot.topWindow()] = std::move(hidden);
  }

  std::vector<QRect> rects = slotsFor(key, area, active);
  const WindowId monocle = model_.view(key).monocle;
  if (monocle != 0 && std::find(active.begin(), active.end(), monocle) != active.end()) {
    // The monocle window fills the area; the others stay where they are.
    rects = {layoutSlots(settings_.layout, area, 1, settings_.gap, settings_.ratio).front()};
    active = {monocle};
  }
  std::vector<std::pair<WindowId, QRect>> placedNow;
  auto placeOne = [&](WindowId hwnd, const QRect& slot) {
    if (moving_.count(hwnd)) return;  // the user holds it: its slot stays empty
    if (current[hwnd] == slot) {
      placed_[hwnd] = slot;
      clamped_.erase(hwnd);
      return;
    }
    const PlaceResult result = placer_.place(hwnd, slot);
    if (!result.ok) {
      // It refused (elevated, gone): leave it alone from now on.
      forget(hwnd, false);
      ignored_[hwnd] = false;
      return;
    }
    // A window that clamped keeps its slot and leaves a gap; its achieved bounds are what the
    // next MoveSizeEnd is compared with.
    placed_[hwnd] = result.achieved;
    placedNow.emplace_back(hwnd, slot);
  };
  for (size_t i = 0; i < active.size() && i < rects.size(); ++i) {
    placeOne(active[i], rects[i]);
    for (const WindowId hidden : beneath[active[i]]) placeOne(hidden, rects[i]);
  }
  raiseNewTops(key, active);
  if (!placedNow.empty()) {
    defer(kVerifyMs, [this, placedNow] { verifyPlacements(placedNow); });
  }
}

// A stack shows its top: when the top of a stack is not the one the last retile left on top, it
// is brought to the front of the windows it shares a rectangle with.
void TilingEngine::raiseNewTops(const WorkspaceKey& key, const std::vector<WindowId>& active) {
  std::set<WindowId> before;
  std::set<WindowId> tops;
  for (const Slot& slot : model_.slotList(key)) {
    for (const WindowId hwnd : slot.windows) {
      if (wasTop_.erase(hwnd) != 0) before.insert(hwnd);
    }
    if (slot.windows.size() > 1) tops.insert(slot.topWindow());
  }
  for (const WindowId top : tops) {
    wasTop_.insert(top);
    if (before.count(top) == 0 && std::find(active.begin(), active.end(), top) != active.end()) {
      placer_.raise(top);
    }
  }
}

void TilingEngine::verifyPlacements(const std::vector<std::pair<WindowId, QRect>>& placed) {
  for (const auto& [hwnd, slot] : placed) {
    if (!model_.contains(hwnd) || moving_.count(hwnd)) continue;
    const std::optional<WindowFacts> facts = placer_.query(hwnd);
    if (!facts) continue;
    placed_[hwnd] = facts->rect;
    // No second placement: a window that cannot take its slot keeps what it took.
    if (differs(facts->rect, slot)) {
      clamped_.insert(hwnd);
    } else {
      clamped_.erase(hwnd);
    }
  }
}

std::vector<QRect> TilingEngine::slotsFor(const WorkspaceKey& key, const QRect& area,
                                          const std::vector<WindowId>& active) const {
  const WorkspaceView& view = model_.view(key);
  std::vector<double> weights;
  weights.reserve(active.size());
  for (const WindowId hwnd : active) {
    const auto found = view.weights.find(hwnd);
    weights.push_back(found == view.weights.end() ? 1.0 : found->second);
  }
  std::vector<QRect> rects =
      layoutSlots(model_.layout(key, settings_.layout), area, static_cast<int>(active.size()),
                  settings_.gap, view.ratio.value_or(settings_.ratio), weights);
  if (view.flipHorizontal || view.flipVertical) {
    rects = flipSlots(std::move(rects), area, view.flipHorizontal, view.flipVertical);
  }
  return rects;
}

// --- commands

FloatOutcome TilingEngine::setForegroundFloating(std::optional<bool> floating) {
  if (!running_) return FloatOutcome::NoWindow;
  const std::optional<WindowId> front = placer_.foreground();
  if (!front) return FloatOutcome::NoWindow;
  const WindowId hwnd = *front;
  const bool managed = model_.contains(hwnd);
  const bool wantFloat = floating.value_or(managed);
  if (wantFloat) {
    if (!managed) return FloatOutcome::Unchanged;
    const std::optional<WorkspaceKey> key = model_.keyOf(hwnd);
    model_.setFloating(hwnd, true);
    placed_.erase(hwnd);
    if (key) requestRetile(*key);
    return FloatOutcome::Floated;
  }
  if (managed) return FloatOutcome::Unchanged;
  pending_.erase(hwnd);
  evaluate(hwnd, 0, true);
  return model_.contains(hwnd) ? FloatOutcome::Tiled : FloatOutcome::Ineligible;
}

std::optional<WorkspaceKey> TilingEngine::foregroundKey() const {
  if (const std::optional<WindowId> front = placer_.foreground()) {
    if (const auto key = model_.keyOf(*front)) return key;
  }
  return std::nullopt;
}

bool TilingEngine::setWorkspaceLayout(const QString& name) {
  const QString layout = canonicalLayout(name);
  if (layout.isEmpty()) return false;
  if (const std::optional<WorkspaceKey> key = foregroundKey()) {
    model_.setLayout(*key, layout);
    requestRetile(*key);
  } else {
    settings_.layout = layout;
    retileAll();
  }
  return true;
}

bool TilingEngine::focusStep(int direction) {
  if (!running_ || direction == 0) return false;
  const std::optional<WindowId> front = placer_.foreground();
  std::optional<WorkspaceKey> key = front ? model_.keyOf(*front) : std::nullopt;
  if (!key) {
    for (const WorkspaceKey& candidate : model_.keys()) {
      if (candidate.desktop == currentDesktop_ || currentDesktop_.isEmpty()) {
        key = candidate;
        break;
      }
    }
  }
  if (!key) return false;
  const std::vector<WindowId>& stack = model_.stack(*key);
  if (stack.empty()) return false;
  const auto at = front ? std::find(stack.begin(), stack.end(), *front) : stack.end();
  const int count = static_cast<int>(stack.size());
  int next = 0;
  if (at == stack.end()) {
    next = direction > 0 ? 0 : count - 1;
  } else {
    next = (static_cast<int>(at - stack.begin()) + (direction > 0 ? 1 : count - 1)) % count;
  }
  placer_.activate(stack[static_cast<size_t>(next)]);
  return true;
}

namespace {

CommandResult ok(const QString& message) { return {true, message}; }
CommandResult fail(const QString& message) { return {false, message}; }

QString directionName(Direction direction) {
  switch (direction) {
    case Direction::Left:
      return QStringLiteral("left");
    case Direction::Right:
      return QStringLiteral("right");
    case Direction::Up:
      return QStringLiteral("up");
    case Direction::Down:
      return QStringLiteral("down");
  }
  return {};
}

}  // namespace

std::optional<TilingEngine::Focused> TilingEngine::focusedManaged() const {
  if (const std::optional<WindowId> front = placer_.foreground()) {
    if (const auto key = model_.keyOf(*front)) return Focused{*key, *front};
  }
  return std::nullopt;
}

std::vector<WindowId> TilingEngine::activeWindows(const WorkspaceKey& key) {
  std::vector<WindowId> active;
  for (const WindowId hwnd : model_.stack(key)) {
    const std::optional<WindowFacts> facts = placer_.query(hwnd);
    if (!facts || !facts->visible || facts->iconic || facts->zoomed || facts->cloaked) continue;
    active.push_back(hwnd);
  }
  return active;
}

CommandResult TilingEngine::focusDirection(Direction direction) {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  const QRect area = placer_.workArea(focused->key.monitor);
  const std::vector<WindowId> active = activeWindows(focused->key);
  const auto at = std::find(active.begin(), active.end(), focused->hwnd);
  if (area.isEmpty() || at == active.end()) {
    return fail(QStringLiteral("the focused window has no slot"));
  }
  const std::vector<QRect> rects = slotsFor(focused->key, area, active);
  const std::optional<size_t> next =
      neighbourSlot(rects, static_cast<size_t>(at - active.begin()), direction);
  if (!next) return fail(QStringLiteral("no window to the %1").arg(directionName(direction)));
  placer_.activate(active[*next]);
  return ok(QStringLiteral("focused"));
}

CommandResult TilingEngine::moveDirection(Direction direction) {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  const QRect area = placer_.workArea(focused->key.monitor);
  const std::vector<WindowId> active = activeWindows(focused->key);
  const auto at = std::find(active.begin(), active.end(), focused->hwnd);
  if (area.isEmpty() || at == active.end()) {
    return fail(QStringLiteral("the focused window has no slot"));
  }
  const std::vector<QRect> rects = slotsFor(focused->key, area, active);
  const std::optional<size_t> next =
      neighbourSlot(rects, static_cast<size_t>(at - active.begin()), direction);
  if (!next) return fail(QStringLiteral("no window to the %1").arg(directionName(direction)));
  model_.swap(focused->hwnd, active[*next]);
  requestRetile(focused->key);
  return ok(QStringLiteral("moved"));
}

CommandResult TilingEngine::stackDirection(Direction direction) {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  const QRect area = placer_.workArea(focused->key.monitor);
  const std::vector<WindowId> active = activeWindows(focused->key);
  const auto at = std::find(active.begin(), active.end(), focused->hwnd);
  if (area.isEmpty() || at == active.end()) {
    return fail(QStringLiteral("the focused window has no slot"));
  }
  const std::vector<QRect> rects = slotsFor(focused->key, area, active);
  const std::optional<size_t> next =
      neighbourSlot(rects, static_cast<size_t>(at - active.begin()), direction);
  if (!next) return fail(QStringLiteral("no window to the %1").arg(directionName(direction)));
  if (!model_.stackOnto(focused->hwnd, active[*next])) {
    return fail(QStringLiteral("cannot stack onto that window"));
  }
  requestRetile(focused->key);
  return ok(QStringLiteral("stacked"));
}

CommandResult TilingEngine::unstack() {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  if (!model_.unstack(focused->hwnd)) {
    return fail(QStringLiteral("the focused window is not stacked"));
  }
  requestRetile(focused->key);
  return ok(QStringLiteral("unstacked"));
}

CommandResult TilingEngine::cycleStack(int step) {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  const WindowId top = model_.cycle(focused->hwnd, step);
  if (top == 0) return fail(QStringLiteral("the focused window is not stacked"));
  requestRetile(focused->key);
  placer_.activate(top);
  return ok(QStringLiteral("cycled"));
}

CommandResult TilingEngine::promote() {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  if (!model_.promote(focused->hwnd)) return ok(QStringLiteral("already the main window"));
  requestRetile(focused->key);
  return ok(QStringLiteral("promoted"));
}

CommandResult TilingEngine::resize(Axis axis, int sign) {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  if (sign == 0) return fail(QStringLiteral("no direction"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  const WorkspaceKey& key = focused->key;
  if (canonicalLayout(model_.layout(key, settings_.layout)) != QLatin1String("mainStack")) {
    return fail(QStringLiteral("only the mainStack layout resizes"));
  }
  const std::vector<WindowId> active = activeWindows(key);
  const auto at = std::find(active.begin(), active.end(), focused->hwnd);
  if (at == active.end() || active.size() < 2) return ok(QStringLiteral("unchanged"));
  const bool isMain = at == active.begin();
  WorkspaceView& view = model_.edit(key);
  if (axis == Axis::Horizontal) {
    // A window of the stack grows by taking width from the main window, and the other way round.
    const double delta = (isMain ? 1 : -1) * (sign > 0 ? 1 : -1) * kRatioStep;
    const double current = view.ratio.value_or(settings_.ratio);
    const double next = std::clamp(current + delta, 0.1, 0.9);
    if (next == current) return ok(QStringLiteral("unchanged"));
    view.ratio = next;
  } else {
    if (isMain || active.size() < 3) return ok(QStringLiteral("unchanged"));
    const auto found = view.weights.find(focused->hwnd);
    const double current = found == view.weights.end() ? 1.0 : found->second;
    const double next =
        std::clamp(current + (sign > 0 ? kWeightStep : -kWeightStep), kMinWeight, kMaxWeight);
    if (next == current) return ok(QStringLiteral("unchanged"));
    view.weights[focused->hwnd] = next;
  }
  requestRetile(key);
  return ok(QStringLiteral("resized"));
}

CommandResult TilingEngine::flip(Axis axis) {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  WorkspaceView& view = model_.edit(focused->key);
  bool& flag = axis == Axis::Horizontal ? view.flipHorizontal : view.flipVertical;
  flag = !flag;
  requestRetile(focused->key);
  return ok(
      QStringLiteral("flip %1 %2")
          .arg(axis == Axis::Horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical"),
               flag ? QStringLiteral("on") : QStringLiteral("off")));
}

CommandResult TilingEngine::monocle() {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  const std::optional<Focused> focused = focusedManaged();
  if (!focused) return fail(QStringLiteral("the focused window is not tiled"));
  WorkspaceView& view = model_.edit(focused->key);
  const bool off = view.monocle == focused->hwnd;
  view.monocle = off ? 0 : focused->hwnd;
  requestRetile(focused->key);
  return ok(off ? QStringLiteral("monocle off") : QStringLiteral("monocle on"));
}

CommandResult TilingEngine::togglePause() {
  if (!running_) return fail(QStringLiteral("the tiler is not running"));
  paused_ = !paused_;
  if (!paused_) retileAll();
  return ok(paused_ ? QStringLiteral("paused") : QStringLiteral("resumed"));
}

std::optional<WindowId> TilingEngine::actionableForeground(QString* error) {
  const std::optional<WindowId> front = placer_.foreground();
  const std::optional<WindowFacts> facts = front ? placer_.query(*front) : std::nullopt;
  if (!facts || !facts->visible) {
    *error = QStringLiteral("no focused window");
    return std::nullopt;
  }
  if (facts->ownProcess || isBuiltinIgnoredClass(facts->className)) {
    *error = QStringLiteral("the focused window is not an application window");
    return std::nullopt;
  }
  return front;
}

CommandResult TilingEngine::closeForeground() {
  QString error;
  const std::optional<WindowId> hwnd = actionableForeground(&error);
  if (!hwnd) return fail(error);
  return placer_.close(*hwnd) ? ok(QStringLiteral("closing"))
                              : fail(QStringLiteral("close failed"));
}

CommandResult TilingEngine::minimizeForeground() {
  QString error;
  const std::optional<WindowId> hwnd = actionableForeground(&error);
  if (!hwnd) return fail(error);
  return placer_.minimize(*hwnd) ? ok(QStringLiteral("minimized"))
                                 : fail(QStringLiteral("minimize failed"));
}

TilingStatus TilingEngine::status() const {
  TilingStatus out;
  out.running = running_;
  out.desktop = currentDesktop_;
  out.managed = model_.managedCount();
  out.floating = model_.floatingCount();
  out.workspaces = static_cast<int>(model_.keys().size());
  out.stacks = model_.stackCount();
  out.clamped = static_cast<int>(clamped_.size());
  out.layout = settings_.layout;
  out.paused = paused_;
  return out;
}

}  // namespace waku::tiling

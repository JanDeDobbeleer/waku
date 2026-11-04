#include "features/workspaces/qml/virtual_desktops_service.h"

#include <QVariantMap>

#include "kernel/qt/live_desktop.h"

namespace waku::workspaces {

VirtualDesktops::VirtualDesktops(QObject* parent) : QObject(parent) {
  if (kernel::liveDesktop(QStringLiteral("virtual_desktops"))) {
    system_ = vdesk::makeWin32VirtualDesktopSystem();
  }
  timer_.setInterval(kPollIntervalMs);
  refreshTimer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
  connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
  if (system_) {
    installHook();
    refresh();
    timer_.start();
  }
}

VirtualDesktops::VirtualDesktops(std::unique_ptr<vdesk::VirtualDesktopSystem> system,
                                 QObject* parent)
    : QObject(parent), system_(std::move(system)) {
  refreshTimer_.setSingleShot(true);
  connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
  if (system_) installHook();
  refresh();
}

VirtualDesktops::~VirtualDesktops() = default;

void VirtualDesktops::installHook() {
  // The hook runs on the GUI thread and only schedules a read.
  const bool active = system_->setChangedHook([this] { scheduleRefresh(kCoalesceMs); });
  timer_.setInterval(active ? kSafetyPollIntervalMs : kPollIntervalMs);
}

void VirtualDesktops::scheduleRefresh(int ms) {
  if (refreshTimer_.isActive() && refreshTimer_.remainingTime() <= ms) return;
  refreshTimer_.start(ms);
}

void VirtualDesktops::setAnimate(bool animate) {
  if (animate == animate_) return;
  animate_ = animate;
  if (system_) system_->setAnimate(animate);
  emit animateChanged();
}

bool VirtualDesktops::refresh() {
  bool available = false;
  QVariantList desktops;
  int current = -1;
  if (system_) {
    if (const std::optional<vdesk::State> state = system_->read()) {
      available = true;
      current = state->current;
      int index = 0;
      for (const vdesk::Desktop& desktop : state->desktops) {
        QVariantList windows;
        for (const quint64 hwnd : desktop.windows) windows << static_cast<qlonglong>(hwnd);
        desktops << QVariantMap{
            {QStringLiteral("id"), desktop.id},
            {QStringLiteral("name"), desktop.name},
            {QStringLiteral("label"),
             desktop.name.isEmpty() ? QString::number(index + 1) : desktop.name},
            {QStringLiteral("index"), index},
            {QStringLiteral("current"), index == current},
            {QStringLiteral("windowCount"), static_cast<int>(desktop.windows.size())},
            {QStringLiteral("windows"), windows}};
        ++index;
      }
    }
  }
  const bool canRemove = available && desktops.size() > 1 && system_ && system_->canRemove();
  const bool differs = available != available_ || current != current_ || canRemove != canRemove_ ||
                       desktops != desktops_;
  available_ = available;
  canRemove_ = canRemove;
  current_ = current;
  desktops_ = std::move(desktops);
  if (differs) emit changed();
  return differs;
}

void VirtualDesktops::switchTo(int index) {
  if (!system_ || index < 0 || current_ < 0 || index == current_) return;
  const int count = static_cast<int>(desktops_.size());
  if (index < count) {
    if (!system_->jumpTo(index)) return;
  } else if (index == count && count < kMaxDesktops) {
    // A system that makes the desktop without showing it names its index; show it, so a new
    // desktop appears as it does from the keyboard. The others (Win+Ctrl+D) show it themselves.
    const int created = system_->create();
    if (created >= 0) system_->jumpTo(created);
  } else {
    return;
  }
  // The switch animates; look again soon instead of waiting for the poll.
  scheduleRefresh(150);
}

void VirtualDesktops::remove(int index) {
  const int count = static_cast<int>(desktops_.size());
  if (!system_ || !canRemove_ || index < 0 || index >= count || count < 2) return;
  if (!system_->remove(index)) return;
  scheduleRefresh(150);  // the notifications and the registry watch usually come first
}

void VirtualDesktops::rename(int index, const QString& name) {
  if (!system_ || index < 0 || index >= static_cast<int>(desktops_.size())) return;
  if (system_->rename(index, name)) scheduleRefresh(150);
}

void VirtualDesktops::stepBy(int direction) {
  const int count = static_cast<int>(desktops_.size());
  if (count == 0 || current_ < 0) return;
  // Wrap like the workspaces of a tiling manager do.
  switchTo(((current_ + direction) % count + count) % count);
}

}  // namespace waku::workspaces

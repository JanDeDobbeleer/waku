#include "features/workspaces/qml/virtual_desktops_service.h"

#include <QVariantMap>

#include "kernel/qt/live_desktop.h"

namespace waku::workspaces {

VirtualDesktops::VirtualDesktops(QObject* parent) : QObject(parent) {
  if (kernel::liveDesktop(QStringLiteral("virtual_desktops"))) {
    system_ = vdesk::makeWin32VirtualDesktopSystem();
  }
  timer_.setInterval(kPollIntervalMs);
  connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
  if (system_) {
    refresh();
    timer_.start();
  }
}

VirtualDesktops::VirtualDesktops(std::unique_ptr<vdesk::VirtualDesktopSystem> system,
                                 QObject* parent)
    : QObject(parent), system_(std::move(system)) {
  refresh();
}

VirtualDesktops::~VirtualDesktops() = default;

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
  const bool differs = available != available_ || current != current_ || desktops != desktops_;
  available_ = available;
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
    system_->create();  // Windows shows the new desktop
  } else {
    return;
  }
  // The switch animates; look again soon instead of waiting for the poll.
  QTimer::singleShot(150, this, [this] { refresh(); });
}

void VirtualDesktops::stepBy(int direction) {
  const int count = static_cast<int>(desktops_.size());
  if (count == 0 || current_ < 0) return;
  // Wrap like the workspaces of a tiling manager do.
  switchTo(((current_ + direction) % count + count) % count);
}

}  // namespace waku::workspaces

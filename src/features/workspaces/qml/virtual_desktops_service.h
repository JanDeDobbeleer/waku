#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "features/workspaces/virtual_desktops.h"

namespace waku::workspaces {

// The Windows virtual desktops as a QML singleton, for a bar without a tiling window manager.
// The Workspaces singleton builds on it; a widget may also read it directly.
//
// `available` is true while Windows exposes the desktop list. `desktops` is
// [{id, name, label, index, current, windowCount, windows}] in Task View order; `label` is the
// name, or the 1-based index of a desktop without one. `windows` is the HWNDs (numbers, ascending)
// of the desktop's windows: top-level, titled, unowned, not of this process; minimized ones stay on
// their desktop and a pinned window counts on the shown desktop. A shell joins them with the
// taskbar rows by hwnd. `current` is the index of the shown desktop (-1
// when unknown). Windows sends no documented change notification for desktops, so the singleton
// polls (every 500 ms) while it exists and `changed()` fires only when the list differs. GUI
// thread only; no worker thread. Without a live desktop (the offscreen platform) there is no
// system and `available` stays false.
class VirtualDesktops : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList desktops READ desktops NOTIFY changed FINAL)
  Q_PROPERTY(int current READ current NOTIFY changed FINAL)

 public:
  static constexpr int kPollIntervalMs = 500;

  explicit VirtualDesktops(QObject* parent = nullptr);
  // Test seam: a fake system; null means none. The timer does not start; call refresh().
  VirtualDesktops(std::unique_ptr<vdesk::VirtualDesktopSystem> system, QObject* parent);
  ~VirtualDesktops() override;

  bool available() const { return available_; }
  QVariantList desktops() const { return desktops_; }
  int current() const { return current_; }

  // Most desktops the singleton makes.
  static constexpr int kMaxDesktops = 20;

  // Shows the desktop at `index` (Task View order) in one jump. The index just past the last
  // desktop (below kMaxDesktops) creates the next one, like a Hyprland workspace that does not
  // exist yet. Other unknown indexes and the shown desktop are ignored.
  Q_INVOKABLE void switchTo(int index);
  Q_INVOKABLE void next() { stepBy(1); }
  Q_INVOKABLE void previous() { stepBy(-1); }
  // Reads the system now; true when the model changed. A shell may call it to look sooner than
  // the poll, e.g. when the taskbar rows change.
  Q_INVOKABLE bool refresh();

 signals:
  void changed();

 private:
  void stepBy(int direction);

  std::unique_ptr<vdesk::VirtualDesktopSystem> system_;
  QTimer timer_;
  bool available_ = false;
  QVariantList desktops_;
  int current_ = -1;
};

}  // namespace waku::workspaces

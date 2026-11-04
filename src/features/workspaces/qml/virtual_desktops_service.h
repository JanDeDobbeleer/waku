#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "features/workspaces/desktop_command.h"
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
// when unknown). `canRemove` is true when remove() can close a desktop (the shell interface is
// open and more than one desktop exists).
//
// Refresh: the system's change hook (the shell's desktop notifications and a registry watch)
// schedules a read after 40 ms, coalesced; a slow poll (5 s) stays as a safety net and for the
// window lists, which no notification covers. Where no hook is active the singleton polls every
// 500 ms. `changed()` fires only when the model differs. GUI thread only; no worker thread.
// Without a live desktop (the offscreen platform) there is no system and `available` stays
// false.
class VirtualDesktops : public QObject, public DesktopControl {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList desktops READ desktops NOTIFY changed FINAL)
  Q_PROPERTY(int current READ current NOTIFY changed FINAL)
  Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed FINAL)
  Q_PROPERTY(bool animate READ animate WRITE setAnimate NOTIFY animateChanged FINAL)

 public:
  static constexpr int kPollIntervalMs = 500;         // without a change hook
  static constexpr int kSafetyPollIntervalMs = 5000;  // with one
  static constexpr int kCoalesceMs = 40;

  explicit VirtualDesktops(QObject* parent = nullptr);
  // Test seam: a fake system; null means none. The timer does not start; call refresh().
  VirtualDesktops(std::unique_ptr<vdesk::VirtualDesktopSystem> system, QObject* parent);
  ~VirtualDesktops() override;

  bool available() const { return available_; }
  QVariantList desktops() const { return desktops_; }
  int current() const { return current_; }
  bool canRemove() const { return canRemove_; }
  // Whether a switch animates like Task View (default) or happens at once.
  bool animate() const { return animate_; }
  void setAnimate(bool animate);

  // Most desktops the singleton makes.
  static constexpr int kMaxDesktops = 20;

  // Shows the desktop at `index` (Task View order) in one jump. The index just past the last
  // desktop (below kMaxDesktops) creates the next one, like a Hyprland workspace that does not
  // exist yet. Other unknown indexes and the shown desktop are ignored.
  Q_INVOKABLE void switchTo(int index);
  // Closes the desktop at `index` like Task View's close button: no visible switch, its windows go
  // to the left neighbour (the first desktop's to the right one). Ignored for a bad index, with
  // only one desktop left, or when `canRemove` is false. If it was the shown desktop, Windows
  // shows the neighbour. The model refreshes by itself.
  Q_INVOKABLE void remove(int index);
  // Names the desktop at `index`; an empty name restores the default. Needs the shell interface.
  Q_INVOKABLE void rename(int index, const QString& name);
  Q_INVOKABLE void next() { stepBy(1); }
  Q_INVOKABLE void previous() { stepBy(-1); }
  // Reads the system now; true when the model changed. A shell may call it to look sooner than
  // the poll, e.g. when the taskbar rows change.
  Q_INVOKABLE bool refresh();

  // DesktopControl: the `desktop` verb of `wakuc` (desktop_command.h). Each reads the system
  // first, so a desktop closed a moment ago is not a target. Numbers count from 1; none creates a
  // desktop (unlike switchTo, which makes the next one).
  QString controlGoto(int number) override;
  QString controlMove(int number) override;
  QString controlStep(int direction) override;
  QString controlStatus() override;

 signals:
  void changed();
  void animateChanged();

 private:
  void stepBy(int direction);
  // Installs the system's change hook and picks the poll interval.
  void installHook();
  // Reads after `ms`, once for any number of requests inside that time.
  void scheduleRefresh(int ms);

  std::unique_ptr<vdesk::VirtualDesktopSystem> system_;
  QTimer timer_;
  QTimer refreshTimer_;
  bool available_ = false;
  bool canRemove_ = false;
  bool animate_ = true;
  QVariantList desktops_;
  int current_ = -1;
};

}  // namespace waku::workspaces

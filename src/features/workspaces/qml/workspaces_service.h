#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include "features/workspaces/qml/virtual_desktops_service.h"

class QJSEngine;
class QQmlEngine;

namespace waku::workspaces {

// The workspaces of the bar: the Windows virtual desktops, as one QML singleton so a widget does
// not depend on how they are read. Built on the VirtualDesktops singleton (shared with any widget
// that uses it directly) and rebuilt whenever it changes. GUI thread only.
//
// monitors: [{handle, name, workspaces: [{id, label, focused, active, hasWindows, windowCount,
//                                         windows}]}]
//   handle  0: virtual desktops belong to no monitor, so there is one entry that every bar shows
//           (match `m.handle === handle || m.handle === 0`)
//   id      opaque to widgets: pass it back to focus() (the desktop index)
//   label   what to show: the desktop's name, or its 1-based index
//   active  the desktop that is shown
//   focused the same as active
//   windows handles (HWND, numbers) of the desktop's windows, ascending. Join them with the
//           taskbar rows (their hwnd role)
// provider: "windows", or "" when Windows does not expose its desktops.
// canRemove: true when remove() can close a desktop: the shell interface is open and more than one
// desktop exists.
class Workspaces : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QString provider READ provider NOTIFY changed FINAL)
  Q_PROPERTY(bool connected READ connected NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList monitors READ monitors NOTIFY changed FINAL)
  Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed FINAL)

 public:
  // `desktops` is borrowed and may be null. It is a sibling singleton, and
  // QQmlEngine::clearSingletons() deletes singletons in its own order on a reload, so it is held
  // as a QPointer and a gone one counts as unavailable.
  explicit Workspaces(VirtualDesktops* desktops = nullptr, QObject* parent = nullptr);
  // The engine's own VirtualDesktops singleton.
  static Workspaces* create(QQmlEngine* engine, QJSEngine* scriptEngine);

  QString provider() const { return provider_; }
  bool connected() const { return !provider_.isEmpty(); }
  QVariantList monitors() const { return monitors_; }
  bool canRemove() const { return canRemove_; }

  // Shows desktop `id` (the monitor handle is ignored: every monitor shows the same desktops).
  // Unknown ids are ignored.
  Q_INVOKABLE void focus(qlonglong monitorHandle, const QString& id);
  // The next or previous desktop in list order, wrapping at the ends.
  Q_INVOKABLE void focusNext(qlonglong monitorHandle) { step(monitorHandle, 1); }
  Q_INVOKABLE void focusPrevious(qlonglong monitorHandle) { step(monitorHandle, -1); }
  // Closes desktop `id` like Task View's close button. Unknown ids are ignored.
  Q_INVOKABLE void remove(qlonglong monitorHandle, const QString& id);

 signals:
  void changed();

 private:
  void rebuild();
  // Recomputes the provider and the model; true when either changed.
  bool update();
  void step(qlonglong monitorHandle, int direction);

  QPointer<VirtualDesktops> desktops_;
  QString provider_;
  bool canRemove_ = false;
  QVariantList monitors_;
};

}  // namespace waku::workspaces

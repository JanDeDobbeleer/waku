#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include "features/workspaces/qml/glazewm_client.h"
#include "features/workspaces/qml/komorebi_client.h"
#include "features/workspaces/qml/virtual_desktops_service.h"

class QJSEngine;
class QQmlEngine;

namespace waku::workspaces {

// The workspaces of whichever tiling window manager is running, as one QML singleton, so a widget
// never mentions komorebi or GlazeWM. With no manager running it shows the Windows virtual
// desktops instead. Built on the Komorebi, GlazeWM and VirtualDesktops singletons (shared with any
// widget that uses them directly) and rebuilt whenever one changes. GUI thread only.
//
// monitors: [{handle, name, workspaces: [{id, label, focused, active, hasWindows, windowCount,
//                                         windows}]}]
//   handle  monitor handle (HMONITOR); match it with Window.window.monitorHandle
//   id      opaque to widgets: pass it back to focus(). komorebi: the workspace index; GlazeWM:
//           the workspace name; Windows: the desktop index
//   label   what to show: the workspace name, or for an unnamed komorebi workspace its 1-based
//           index
//   active  shown on its monitor
//   focused active on the focused monitor (GlazeWM: its global focus)
//   windows handles (HWND, numbers) of the workspace's windows, in the window manager's order
//           (komorebi), or ascending (Windows desktops); empty for GlazeWM. Join them with the
//           taskbar rows (their hwnd role)
// Virtual desktops belong to no monitor: the Windows provider returns one entry with handle 0,
// which every bar shows (match `m.handle === handle || m.handle === 0`).
// provider: "komorebi", "glazewm", "windows" or "". A tiling manager that is connected wins over
// the virtual desktops. When both managers are connected, `prefer` decides (default "komorebi").
// `prefer` "windows" shows the virtual desktops even while a manager runs.
// canRemove: true when remove() can close a workspace: the "windows" provider with the shell
// interface open and more than one desktop. The tiling managers have no such call here.
class Workspaces : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QString provider READ provider NOTIFY changed FINAL)
  Q_PROPERTY(bool connected READ connected NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList monitors READ monitors NOTIFY changed FINAL)
  Q_PROPERTY(QString prefer READ prefer WRITE setPrefer NOTIFY changed FINAL)
  Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed FINAL)

 public:
  // The backends are borrowed; any may be null. They are sibling singletons, and
  // QQmlEngine::clearSingletons() deletes them in its own order on a reload, so either may go
  // first: they are held as QPointer and a gone one counts as disconnected.
  Workspaces(Komorebi* komorebi, GlazeWM* glazewm, VirtualDesktops* desktops = nullptr,
             QObject* parent = nullptr);
  // The engine's own Komorebi, GlazeWM and VirtualDesktops singletons.
  static Workspaces* create(QQmlEngine* engine, QJSEngine* scriptEngine);

  QString provider() const { return provider_; }
  bool connected() const { return !provider_.isEmpty(); }
  QVariantList monitors() const { return monitors_; }
  QString prefer() const { return prefer_; }
  void setPrefer(const QString& prefer);
  bool canRemove() const;

  // Shows workspace `id` on the monitor with this handle. Unknown monitors and ids are ignored.
  Q_INVOKABLE void focus(qlonglong monitorHandle, const QString& id);
  // The next or previous workspace of that monitor in list order, wrapping at the ends.
  Q_INVOKABLE void focusNext(qlonglong monitorHandle) { step(monitorHandle, 1); }
  Q_INVOKABLE void focusPrevious(qlonglong monitorHandle) { step(monitorHandle, -1); }
  // Closes workspace `id` (the same id focus() takes) like Task View's close button. Only the
  // "windows" provider closes anything; the monitor handle is ignored (desktops are not per
  // monitor). Other providers and unknown ids are ignored.
  Q_INVOKABLE void remove(qlonglong monitorHandle, const QString& id);

 signals:
  void changed();

 private:
  void rebuild();
  // Recomputes the provider and the model; true when either changed.
  bool update();
  void step(qlonglong monitorHandle, int direction);

  QPointer<Komorebi> komorebi_;
  QPointer<GlazeWM> glazewm_;
  QPointer<VirtualDesktops> desktops_;
  QString prefer_ = QStringLiteral("komorebi");
  QString provider_;
  bool canRemove_ = false;
  QVariantList monitors_;
};

}  // namespace waku::workspaces

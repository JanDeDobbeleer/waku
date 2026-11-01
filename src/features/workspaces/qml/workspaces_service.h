#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include "features/workspaces/qml/glazewm_client.h"
#include "features/workspaces/qml/komorebi_client.h"

class QJSEngine;
class QQmlEngine;

namespace wabar::workspaces {

// The workspaces of whichever tiling window manager is running, as one QML singleton, so a widget
// never mentions komorebi or GlazeWM. Built on the Komorebi and GlazeWM singletons (shared with
// any widget that uses them directly) and rebuilt whenever either changes. GUI thread only.
//
// monitors: [{handle, name, workspaces: [{id, label, focused, active, hasWindows, windowCount,
//                                         windows}]}]
//   handle  monitor handle (HMONITOR); match it with Window.window.monitorHandle
//   id      opaque to widgets: pass it back to focus(). komorebi: the workspace index; GlazeWM:
//           the workspace name
//   label   what to show: the workspace name, or for an unnamed komorebi workspace its 1-based
//           index
//   active  shown on its monitor
//   focused active on the focused monitor (GlazeWM: its global focus)
//   windows handles (HWND, numbers) of the workspace's windows, in the window manager's order;
//           komorebi only, empty with GlazeWM. Join them with the taskbar rows (their hwnd role)
// provider: "komorebi", "glazewm" or "" when neither is connected. When both are connected,
// `prefer` decides (default "komorebi"; any other value means the default).
class Workspaces : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QString provider READ provider NOTIFY changed FINAL)
  Q_PROPERTY(bool connected READ connected NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList monitors READ monitors NOTIFY changed FINAL)
  Q_PROPERTY(QString prefer READ prefer WRITE setPrefer NOTIFY changed FINAL)

 public:
  // Both backends are borrowed; either may be null. They are sibling singletons, and
  // QQmlEngine::clearSingletons() deletes them in its own order on a reload, so either may go
  // first: they are held as QPointer and a gone one counts as disconnected.
  Workspaces(Komorebi* komorebi, GlazeWM* glazewm, QObject* parent = nullptr);
  // The engine's own Komorebi and GlazeWM singletons.
  static Workspaces* create(QQmlEngine* engine, QJSEngine* scriptEngine);

  QString provider() const { return provider_; }
  bool connected() const { return !provider_.isEmpty(); }
  QVariantList monitors() const { return monitors_; }
  QString prefer() const { return prefer_; }
  void setPrefer(const QString& prefer);

  // Shows workspace `id` on the monitor with this handle. Unknown monitors and ids are ignored.
  Q_INVOKABLE void focus(qlonglong monitorHandle, const QString& id);
  // The next or previous workspace of that monitor in list order, wrapping at the ends.
  Q_INVOKABLE void focusNext(qlonglong monitorHandle) { step(monitorHandle, 1); }
  Q_INVOKABLE void focusPrevious(qlonglong monitorHandle) { step(monitorHandle, -1); }

 signals:
  void changed();

 private:
  void rebuild();
  void step(qlonglong monitorHandle, int direction);

  QPointer<Komorebi> komorebi_;
  QPointer<GlazeWM> glazewm_;
  QString prefer_ = QStringLiteral("komorebi");
  QString provider_;
  QVariantList monitors_;
};

}  // namespace wabar::workspaces

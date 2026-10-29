#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "core/widgets/event_bus.h"
#include "widgets/komorebi/komorebi_service.h"

namespace tidy::services {

namespace platform = ::yasb::platform;
namespace widgets = ::yasb::widgets;

// komorebi as a QML singleton, shared by every widget. Wraps widgets::komorebi::KomorebiService
// (the named-pipe listener and `komorebic` commands) behind a private EventBus; the singleton
// subscribes when the engine first creates it. GUI thread only: the pipe thread parses JSON and
// posts results to the GUI thread. The service unsubscribes and joins the pipe thread when the
// engine destroys the singleton.
//
// `monitors`: [{index, id, name, focused, workspaces: [{index, name, focused, hasWindows,
// windowCount}], workspace, layout}]; `id` is the monitor handle (HMONITOR). A monitor is `focused`
// when it is the focused one; a workspace is `focused` when it is the one shown on its monitor.
// `workspace` is the index of the shown workspace (-1 when none). `layout` is what that workspace
// shows: "bsp", "columns", "rows", "grid", "scrolling", "vertical_stack", "horizontal_stack",
// "ultrawide_vertical_stack", "right_main_vertical_stack", "custom", or one of the states "paused",
// "floating", "maximized", "monocle" (checked in that order, as Python YASB does).
class Komorebi : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool connected READ connected NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList monitors READ monitors NOTIFY changed FINAL)

 public:
  explicit Komorebi(QObject* parent = nullptr);
  // Test seam: a fake komorebi backend, command runner and pipe name.
  Komorebi(platform::KomorebiBackend backend,
           widgets::komorebi::KomorebiService::CommandRunner runner, QString pipeName,
           platform::KomorebiPipeListener::Timing timing, QObject* parent);
  ~Komorebi() override;

  bool connected() const { return connected_; }
  QVariantList monitors() const { return monitors_; }

  // Test seam: the bus the service publishes its state on.
  widgets::EventBus& bus() { return bus_; }

  Q_INVOKABLE void focusWorkspace(int monitorIndex, int workspaceIndex);
  // Sets the layout of the shown workspace (`komorebic workspace-layout`); `layout` is a snake_case
  // or kebab-case name.
  Q_INVOKABLE void setLayout(int monitorIndex, QString layout);
  // Moves `step` places along `layouts` from the current layout (wrapping); in a blocking state
  // (paused, floating, maximized, monocle) it undoes that state instead.
  Q_INVOKABLE void cycleLayout(int monitorIndex, QStringList layouts, int step);

 signals:
  void changed();

 private:
  void subscribe();
  void onEvent(const QString& name, const QVariantMap& payload);
  QVariantMap monitorAt(int monitorIndex) const;

  widgets::EventBus bus_;
  widgets::komorebi::KomorebiService service_;
  bool connected_ = false;
  QVariantList monitors_;
};

}  // namespace tidy::services

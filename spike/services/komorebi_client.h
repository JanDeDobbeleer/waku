#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QVariantList>

#include "core/widgets/event_bus.h"
#include "widgets/komorebi/komorebi_service.h"

namespace barnacle::services {

namespace platform = ::yasb::platform;
namespace widgets = ::yasb::widgets;

// komorebi as a QML singleton, shared by every widget. Wraps widgets::komorebi::KomorebiService
// (the named-pipe listener and `komorebic` commands) behind a private EventBus; the singleton
// subscribes when the engine first creates it. GUI thread only: the pipe thread parses JSON and
// posts results to the GUI thread. The service unsubscribes and joins the pipe thread when the
// engine destroys the singleton.
//
// `monitors`: [{index, id, name, workspaces: [{index, name, focused, hasWindows,
// windowCount}]}]; `id` is the monitor handle (HMONITOR).
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

 signals:
  void changed();

 private:
  void subscribe();
  void onEvent(const QString& name, const QVariantMap& payload);

  widgets::EventBus bus_;
  widgets::komorebi::KomorebiService service_;
  bool connected_ = false;
  QVariantList monitors_;
};

}  // namespace barnacle::services

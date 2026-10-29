#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QVariantList>

#include "core/widgets/event_bus.h"
#include "widgets/glazewm/glazewm_service.h"

namespace wabar::services {

namespace platform = ::yasb::platform;
namespace widgets = ::yasb::widgets;

// GlazeWM as a QML singleton, shared by every widget. Wraps widgets::glazewm::GlazewmService
// (the WebSocket client and its protocol) behind a private EventBus; the singleton subscribes to
// the workspaces, tiling direction and binding mode topics when the engine first creates it.
// GUI thread only: the service posts the socket thread's messages to the GUI thread. The
// service stops its socket (joining the worker thread) when the engine destroys the singleton.
//
// `monitors`: [{name, hwnd, workspaces: [{name, displayName, focused, displayed, windowCount,
// hasWindows}]}]; `hwnd` is the monitor handle (HMONITOR). `bindingMode` is "" when none.
class GlazeWM : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool connected READ connected NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList monitors READ monitors NOTIFY changed FINAL)
  Q_PROPERTY(QString tilingDirection READ tilingDirection NOTIFY changed FINAL)
  Q_PROPERTY(QString bindingMode READ bindingMode NOTIFY changed FINAL)

 public:
  explicit GlazeWM(QObject* parent = nullptr);
  // Test seam: `sender` replaces the socket send, `opener` replaces opening the socket.
  GlazeWM(widgets::glazewm::GlazewmService::Sender sender,
          widgets::glazewm::GlazewmService::Opener opener, QObject* parent);
  ~GlazeWM() override;

  bool connected() const { return connected_; }
  QVariantList monitors() const { return monitors_; }
  QString tilingDirection() const { return tilingDirection_; }
  QString bindingMode() const { return bindingMode_; }

  // Test seam: feed connection and messages as the socket would.
  widgets::glazewm::GlazewmService& service() { return service_; }

  Q_INVOKABLE void activateWorkspace(const QString& name);
  Q_INVOKABLE void focusNextWorkspace();
  Q_INVOKABLE void focusPreviousWorkspace();
  Q_INVOKABLE void toggleTilingDirection();
  Q_INVOKABLE void enableBindingMode(const QString& name);
  Q_INVOKABLE void disableBindingMode(const QString& name);

 signals:
  void changed();

 private:
  void subscribeAll();
  void onEvent(const QString& name, const QVariantMap& payload);

  widgets::EventBus bus_;
  widgets::glazewm::GlazewmService service_;
  bool connected_ = false;
  QVariantList monitors_;
  QString tilingDirection_ = QStringLiteral("horizontal");
  QString bindingMode_;
};

}  // namespace wabar::services

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "features/workspaces/glazewm_state.h"

namespace wabar::workspaces {

class EventBus;

namespace glazewm {

class GlazewmSocket;

using MonitorsPtr = std::shared_ptr<const std::vector<Monitor>>;
using BindingModePtr = std::shared_ptr<const BindingMode>;

// EventBus protocol between the GlazeWM widgets and GlazewmService:
//  - widgets emit kSubscribeEvent with {"topic": "workspaces"|"tiling_direction"|"binding_mode",
//    "uri": string}. The first subscription opens the socket to its uri (the first uri wins). On
//    every connect the service sends the subscription and query messages of every topic requested
//    so far; a topic requested while connected sends its messages at once. The messages per topic
//    are those of the Python widgets:
//      workspaces:       "sub -e workspace_activated workspace_deactivated workspace_updated
//                         focus_changed focused_container_moved", "query monitors"
//      tiling_direction: "sub -e focus_changed tiling_direction_changed focused_container_moved",
//                         "query tiling-direction"
//      binding_mode:     "sub -e binding_modes_changed", "query binding-modes"
//    Every incoming "event_subscription" message re-sends the query messages of the subscribed
//    topics; "client_response" replies are dispatched on their clientMessage.
//  - the service emits kStateEvent with {"kind": ...}:
//      "connection"       {"connected": bool}
//      "workspaces"       {"monitors": MonitorsPtr}
//      "tiling_direction" {"direction": int, a TilingDirection value}
//      "binding_mode"     {"bindingMode": BindingModePtr}
//    A subscribe replays (queued) the last connection state and the last data of that topic.
//  - widgets emit kCommandEvent with {"command": string}; the full text ("command focus ...") is
//    sent as one text frame, anything not starting with "command " is dropped. Use the helpers
//    below to build it.
inline const QString kSubscribeEvent = QStringLiteral("glazewm_subscribe");
inline const QString kStateEvent = QStringLiteral("glazewm");
inline const QString kCommandEvent = QStringLiteral("glazewm_command");

inline const QString kTopicWorkspaces = QStringLiteral("workspaces");
inline const QString kTopicTilingDirection = QStringLiteral("tiling_direction");
inline const QString kTopicBindingMode = QStringLiteral("binding_mode");

MonitorsPtr monitorsFromPayload(const QVariantMap& payload);
BindingModePtr bindingModeFromPayload(const QVariantMap& payload);

// Bus helpers.
void subscribe(EventBus& bus, const QString& topic, const QString& uri);
void sendCommand(EventBus& bus, const QString& fullCommand);

// Command text of the Python client methods.
QString activateWorkspaceCommand(const QString& workspaceName);
QString toggleTilingDirectionCommand();
QString disableBindingModeCommand(const QString& name);
QString enableBindingModeCommand(const QString& name);
QString focusNextWorkspaceCommand();
QString focusPrevWorkspaceCommand();
QString focusNextWorkspaceGlobalCommand();
QString focusPrevWorkspaceGlobalCommand();

// Convenience wrappers: sendCommand(bus, <name>Command(...)).
void activateWorkspace(EventBus& bus, const QString& workspaceName);
void toggleTilingDirection(EventBus& bus);
void disableBindingMode(EventBus& bus, const QString& name);
void enableBindingMode(EventBus& bus, const QString& name);
void focusNextWorkspace(EventBus& bus);
void focusPrevWorkspace(EventBus& bus);
void focusNextWorkspaceGlobal(EventBus& bus);
void focusPrevWorkspaceGlobal(EventBus& bus);

// The single GlazeWM client of one Runtime (port of GlazewmClient). Lives on the GUI thread and
// owns one GlazewmSocket (created by the first subscription); the socket's worker
// thread is described in platform/glazewm.h and its signals arrive queued on the GUI thread.
class GlazewmService : public QObject {
  Q_OBJECT
 public:
  using Sender = std::function<bool(const QByteArray& utf8)>;
  using Opener = std::function<void(const QString& uri)>;

  explicit GlazewmService(EventBus& bus);
  // Test seam: `sender` replaces the socket's sendText, `opener` is called once with the first
  // uri. Feed the socket's signals through handleConnected and handleMessage.
  GlazewmService(EventBus& bus, Sender sender, Opener opener);
  ~GlazewmService() override;

  // Stops the socket; safe to call more than once. Later subscriptions are ignored.
  void stop();
  bool isConnected() const { return connected_; }

  // What the socket's connectedChanged and textMessage signals call.
  void handleConnected(bool connected);
  void handleMessage(const QString& text);

 private:
  void onBusEvent(const QString& name, const QVariantMap& payload);
  void onSubscribe(const QString& topic, const QString& uri);
  void sendTopic(const QString& topic);
  void sendQueries(const QString& topic);
  void send(const QByteArray& message);
  void replay(const QString& topic);
  void emitConnection();
  void emitWorkspaces();
  void emitTilingDirection();
  void emitBindingMode();

  EventBus& bus_;
  Sender sender_;
  Opener opener_;
  std::unique_ptr<GlazewmSocket> socket_;
  QStringList topics_;
  bool opened_ = false;
  bool connected_ = false;
  bool stopped_ = false;
  MonitorsPtr lastMonitors_;
  std::optional<TilingDirection> lastDirection_;
  BindingModePtr lastBindingMode_;
};

}  // namespace glazewm
}  // namespace wabar::workspaces

Q_DECLARE_METATYPE(wabar::workspaces::glazewm::MonitorsPtr)
Q_DECLARE_METATYPE(wabar::workspaces::glazewm::BindingModePtr)

#include "widgets/glazewm/glazewm_service.h"

#include <QDebug>
#include <QMetaObject>
#include <exception>
#include <string>
#include <utility>

#include "core/widgets/event_bus.h"
#include "platform/glazewm.h"

namespace yasb::widgets::glazewm {
namespace {

struct TopicMessages {
  QByteArray sub;
  QByteArray query;
};

bool topicMessages(const QString& topic, TopicMessages* out) {
  if (topic == kTopicWorkspaces) {
    *out = {
        "sub -e workspace_activated workspace_deactivated workspace_updated focus_changed "
        "focused_container_moved",
        "query monitors"};
  } else if (topic == kTopicTilingDirection) {
    *out = {"sub -e focus_changed tiling_direction_changed focused_container_moved",
            "query tiling-direction"};
  } else if (topic == kTopicBindingMode) {
    *out = {"sub -e binding_modes_changed", "query binding-modes"};
  } else {
    return false;
  }
  return true;
}

}  // namespace

MonitorsPtr monitorsFromPayload(const QVariantMap& payload) {
  return payload.value(QStringLiteral("monitors")).value<MonitorsPtr>();
}

BindingModePtr bindingModeFromPayload(const QVariantMap& payload) {
  return payload.value(QStringLiteral("bindingMode")).value<BindingModePtr>();
}

void subscribe(EventBus& bus, const QString& topic, const QString& uri) {
  emit bus.event(kSubscribeEvent, {{QStringLiteral("topic"), topic}, {QStringLiteral("uri"), uri}});
}

void sendCommand(EventBus& bus, const QString& fullCommand) {
  emit bus.event(kCommandEvent, {{QStringLiteral("command"), fullCommand}});
}

QString activateWorkspaceCommand(const QString& workspaceName) {
  return QStringLiteral("command focus --workspace ") + workspaceName;
}
QString toggleTilingDirectionCommand() { return QStringLiteral("command toggle-tiling-direction"); }
QString disableBindingModeCommand(const QString& name) {
  return QStringLiteral("command wm-disable-binding-mode --name ") + name;
}
QString enableBindingModeCommand(const QString& name) {
  return QStringLiteral("command wm-enable-binding-mode --name ") + name;
}
QString focusNextWorkspaceCommand() {
  return QStringLiteral("command focus --next-active-workspace-on-monitor");
}
QString focusPrevWorkspaceCommand() {
  return QStringLiteral("command focus --prev-active-workspace-on-monitor");
}
QString focusNextWorkspaceGlobalCommand() {
  return QStringLiteral("command focus --next-active-workspace");
}
QString focusPrevWorkspaceGlobalCommand() {
  return QStringLiteral("command focus --prev-active-workspace");
}

void activateWorkspace(EventBus& bus, const QString& name) {
  sendCommand(bus, activateWorkspaceCommand(name));
}
void toggleTilingDirection(EventBus& bus) { sendCommand(bus, toggleTilingDirectionCommand()); }
void disableBindingMode(EventBus& bus, const QString& name) {
  sendCommand(bus, disableBindingModeCommand(name));
}
void enableBindingMode(EventBus& bus, const QString& name) {
  sendCommand(bus, enableBindingModeCommand(name));
}
void focusNextWorkspace(EventBus& bus) { sendCommand(bus, focusNextWorkspaceCommand()); }
void focusPrevWorkspace(EventBus& bus) { sendCommand(bus, focusPrevWorkspaceCommand()); }
void focusNextWorkspaceGlobal(EventBus& bus) {
  sendCommand(bus, focusNextWorkspaceGlobalCommand());
}
void focusPrevWorkspaceGlobal(EventBus& bus) {
  sendCommand(bus, focusPrevWorkspaceGlobalCommand());
}

GlazewmService::GlazewmService(EventBus& bus) : bus_(bus) {
  sender_ = [this](const QByteArray& utf8) { return socket_ && socket_->sendText(utf8); };
  opener_ = [this](const QString& uri) {
    socket_ = std::make_unique<platform::GlazewmSocket>();
    connect(socket_.get(), &platform::GlazewmSocket::connectedChanged, this,
            &GlazewmService::handleConnected);
    connect(socket_.get(), &platform::GlazewmSocket::textMessage, this,
            &GlazewmService::handleMessage);
    socket_->open(uri);
  };
  connect(&bus_, &EventBus::event, this, &GlazewmService::onBusEvent);
}

GlazewmService::GlazewmService(EventBus& bus, Sender sender, Opener opener)
    : bus_(bus), sender_(std::move(sender)), opener_(std::move(opener)) {
  connect(&bus_, &EventBus::event, this, &GlazewmService::onBusEvent);
}

GlazewmService::~GlazewmService() { stop(); }

void GlazewmService::stop() {
  stopped_ = true;
  connected_ = false;
  if (socket_) {
    socket_->stop();
    socket_.reset();
  }
}

void GlazewmService::onBusEvent(const QString& name, const QVariantMap& payload) {
  if (stopped_) return;
  try {
    if (name == kSubscribeEvent) {
      onSubscribe(payload.value(QStringLiteral("topic")).toString(),
                  payload.value(QStringLiteral("uri")).toString());
    } else if (name == kCommandEvent) {
      const QString command = payload.value(QStringLiteral("command")).toString();
      if (command.startsWith(QStringLiteral("command "))) send(command.toUtf8());
    }
  } catch (const std::exception& e) {
    qWarning() << "GlazeWM bus event failed:" << e.what();
  }
}

void GlazewmService::onSubscribe(const QString& topic, const QString& uri) {
  TopicMessages messages;
  if (!topicMessages(topic, &messages)) return;
  const bool isNew = !topics_.contains(topic);
  if (isNew) topics_.append(topic);
  if (!opened_ && !uri.isEmpty()) {
    opened_ = true;
    if (opener_) opener_(uri);
  }
  if (isNew && connected_) sendTopic(topic);
  // Late subscribers need the current state; queued so the caller has connected first.
  QMetaObject::invokeMethod(this, [this, topic] { replay(topic); }, Qt::QueuedConnection);
}

void GlazewmService::sendTopic(const QString& topic) {
  TopicMessages messages;
  if (!topicMessages(topic, &messages)) return;
  send(messages.sub);
  send(messages.query);
}

void GlazewmService::sendQueries(const QString& topic) {
  TopicMessages messages;
  if (topicMessages(topic, &messages)) send(messages.query);
}

void GlazewmService::send(const QByteArray& message) {
  if (sender_) sender_(message);
}

void GlazewmService::handleConnected(bool connected) {
  if (stopped_) return;
  connected_ = connected;
  if (connected) {
    for (const QString& topic : topics_) sendTopic(topic);
  }
  emitConnection();
}

void GlazewmService::handleMessage(const QString& text) {
  if (stopped_) return;
  try {
    const QByteArray bytes = text.toUtf8();
    const Json response =
        Json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false);
    if (response.is_discarded()) {
      qWarning() << "Received invalid JSON data from GlazeWM.";
      return;
    }
    if (!response.is_object()) return;
    const std::string messageType = response.value("messageType", std::string());
    if (messageType == "event_subscription") {
      for (const QString& topic : topics_) sendQueries(topic);
    } else if (messageType == "client_response") {
      const auto it = response.find("data");
      if (it == response.end() || !it->is_object()) {
        qWarning() << "Expected GlazeWM 'data' to be an object.";
        return;
      }
      const Json& data = *it;
      const std::string clientMessage = response.value("clientMessage", std::string());
      if (clientMessage == "query monitors") {
        if (auto monitors = parseMonitors(data)) {
          lastMonitors_ = std::make_shared<const std::vector<Monitor>>(std::move(*monitors));
          emitWorkspaces();
        }
      } else if (clientMessage == "query tiling-direction") {
        lastDirection_ = parseTilingDirection(data);
        emitTilingDirection();
      } else if (clientMessage == "query binding-modes") {
        if (auto mode = parseBindingModes(data)) {
          lastBindingMode_ = std::make_shared<const BindingMode>(std::move(*mode));
          emitBindingMode();
        }
      }
    }
  } catch (const std::exception& e) {
    qWarning() << "GlazeWM message failed:" << e.what();
  }
}

void GlazewmService::replay(const QString& topic) {
  if (stopped_) return;
  emitConnection();
  if (topic == kTopicWorkspaces && lastMonitors_) {
    emitWorkspaces();
  } else if (topic == kTopicTilingDirection && lastDirection_) {
    emitTilingDirection();
  } else if (topic == kTopicBindingMode && lastBindingMode_) {
    emitBindingMode();
  }
}

void GlazewmService::emitConnection() {
  emit bus_.event(kStateEvent, {{QStringLiteral("kind"), QStringLiteral("connection")},
                                {QStringLiteral("connected"), connected_}});
}

void GlazewmService::emitWorkspaces() {
  emit bus_.event(kStateEvent, {{QStringLiteral("kind"), kTopicWorkspaces},
                                {QStringLiteral("monitors"), QVariant::fromValue(lastMonitors_)}});
}

void GlazewmService::emitTilingDirection() {
  emit bus_.event(kStateEvent, {{QStringLiteral("kind"), kTopicTilingDirection},
                                {QStringLiteral("direction"), static_cast<int>(*lastDirection_)}});
}

void GlazewmService::emitBindingMode() {
  emit bus_.event(kStateEvent,
                  {{QStringLiteral("kind"), kTopicBindingMode},
                   {QStringLiteral("bindingMode"), QVariant::fromValue(lastBindingMode_)}});
}

}  // namespace yasb::widgets::glazewm

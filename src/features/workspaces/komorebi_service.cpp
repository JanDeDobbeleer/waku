#include "features/workspaces/komorebi_service.h"

#include <QDebug>
#include <QMetaObject>
#include <QUuid>
#include <QVariantList>
#include <utility>

#include "features/workspaces/command_line.h"
#include "features/workspaces/event_bus.h"
#include "features/workspaces/komorebi_state.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/shell_launch.h"

namespace waku::workspaces::komorebi {
namespace {

JsonPtr parse(const QByteArray& bytes) {
  auto value = nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr,
                                     /*allow_exceptions=*/false);
  if (value.is_discarded()) return nullptr;
  return std::make_shared<const nlohmann::json>(std::move(value));
}

void runCommands(const std::vector<QStringList>& commands) {
  if (commands.empty()) return;
  if (commands.size() == 1) {
    launchKomorebic(commands.front());
  } else {
    waku::kernel::launchHiddenShell(KomorebiService::shellChain(commands));
  }
}

}  // namespace

JsonPtr jsonFromPayload(const QVariantMap& payload, const QString& key) {
  return payload.value(key).value<JsonPtr>();
}

void sendCommand(EventBus& bus, const QStringList& args) { sendCommandChain(bus, {args}); }

void sendCommandChain(EventBus& bus, const std::vector<QStringList>& commands) {
  QVariantList list;
  for (const QStringList& args : commands) list.append(QVariant(args));
  emit bus.event(kCommandEvent, {{QStringLiteral("commands"), list}});
}

KomorebiService::KomorebiService(EventBus& bus)
    : KomorebiService(bus, defaultKomorebiBackend(), &runCommands, uniquePipeName(), {}) {
  if (!kernel::liveDesktop(QStringLiteral("komorebi"))) {
    live_ = false;
    runner_ = nullptr;
  }
}

KomorebiService::KomorebiService(EventBus& bus, KomorebiBackend backend, CommandRunner runner,
                                 QString pipeName, KomorebiPipeListener::Timing timing)
    : bus_(bus),
      backend_(std::move(backend)),
      runner_(std::move(runner)),
      pipeName_(std::move(pipeName)),
      timing_(timing) {
  connect(&bus_, &EventBus::event, this, &KomorebiService::onBusEvent);
}

KomorebiService::~KomorebiService() { stop(); }

QString KomorebiService::uniquePipeName() {
  return QStringLiteral("waku-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString KomorebiService::shellChain(const std::vector<QStringList>& commands) {
  QStringList parts;
  for (const QStringList& args : commands) {
    parts.append(QStringLiteral("\"komorebic.exe\" ") + quoteCmdLine(args));
  }
  return parts.join(QStringLiteral(" && "));
}

void KomorebiService::stop() {
  stopped_ = true;
  if (listener_) listener_->stop();
  connected_ = false;
}

void KomorebiService::onBusEvent(const QString& name, const QVariantMap& payload) {
  if (name == kSubscribeEvent) {
    ensureStarted();
    if (connected_ && lastState_) {
      // Late subscribers (e.g. a bar created after connect) need the current state.
      QMetaObject::invokeMethod(
          this,
          [this] {
            if (connected_ && lastState_) deliverConnect(lastState_);
          },
          Qt::QueuedConnection);
    }
  } else if (name == kCommandEvent) {
    std::vector<QStringList> commands;
    for (const QVariant& entry : payload.value(QStringLiteral("commands")).toList()) {
      const QStringList args = entry.toStringList();
      if (!args.isEmpty()) commands.push_back(args);
    }
    if (!commands.empty() && runner_) runner_(commands);
  }
}

void KomorebiService::ensureStarted() {
  if (listener_ || stopped_ || !live_) return;
  KomorebiPipeListener::Callbacks callbacks;
  // These run on the listener thread; queued calls to `this` are dropped once it is destroyed.
  callbacks.connected = [this](const QByteArray& bytes) {
    JsonPtr state = parse(bytes);
    if (!state || !state->is_object()) return false;
    QMetaObject::invokeMethod(this, [this, state] { deliverConnect(state); }, Qt::QueuedConnection);
    return true;
  };
  callbacks.message = [this](const QByteArray& bytes) {
    JsonPtr data = parse(bytes);
    if (!data) {
      qWarning().noquote() << "Failed to parse komorebi event:"
                           << QString::fromUtf8(bytes.left(200));
      return;
    }
    if (!data->is_object()) return;
    const auto* event = child(data.get(), "event");
    const auto* state = child(data.get(), "state");
    if (!event || !event->is_object() || !truthy(event) || !truthy(state)) return;
    auto eventPtr = JsonPtr(data, event);
    auto statePtr = JsonPtr(data, state);
    QMetaObject::invokeMethod(
        this, [this, eventPtr, statePtr] { deliverUpdate(eventPtr, statePtr); },
        Qt::QueuedConnection);
  };
  callbacks.disconnected = [this] {
    QMetaObject::invokeMethod(this, [this] { deliverDisconnect(); }, Qt::QueuedConnection);
  };
  listener_ =
      std::make_unique<KomorebiPipeListener>(backend_, std::move(callbacks), pipeName_, timing_);
  listener_->start();
}

void KomorebiService::deliverConnect(JsonPtr state) {
  if (stopped_) return;
  connected_ = true;
  lastState_ = state;
  emit bus_.event(kStateEvent, {{QStringLiteral("kind"), QStringLiteral("connect")},
                                {QStringLiteral("state"), QVariant::fromValue(state)}});
}

void KomorebiService::deliverUpdate(JsonPtr event, JsonPtr state) {
  if (stopped_) return;
  lastState_ = state;
  emit bus_.event(kStateEvent, {{QStringLiteral("kind"), QStringLiteral("update")},
                                {QStringLiteral("event"), QVariant::fromValue(event)},
                                {QStringLiteral("state"), QVariant::fromValue(state)}});
}

void KomorebiService::deliverDisconnect() {
  if (stopped_) return;
  connected_ = false;
  emit bus_.event(kStateEvent, {{QStringLiteral("kind"), QStringLiteral("disconnect")}});
}

}  // namespace waku::workspaces::komorebi

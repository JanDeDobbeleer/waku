#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "platform/komorebi.h"

namespace yasb::widgets {

class EventBus;

namespace komorebi {

// Parsed komorebi JSON shared between widgets without copying (payload values of the "komorebi"
// bus event).
using JsonPtr = std::shared_ptr<const nlohmann::json>;

// EventBus protocol between the komorebi widgets and KomorebiService:
//  - widgets emit kSubscribeEvent ({}) once; the service starts listening on the first request
//    and replays "connect" with the last state when it is already connected;
//  - the service emits kStateEvent with {"kind": "connect"|"update"|"disconnect",
//    "event": JsonPtr (update only), "state": JsonPtr (connect/update)};
//  - widgets emit kCommandEvent with {"commands": QVariantList of QStringList}: one entry runs
//    `komorebic <args>`, several run as one `"komorebic.exe" a && "komorebic.exe" b` shell chain.
inline const QString kSubscribeEvent = QStringLiteral("komorebi_subscribe");
inline const QString kStateEvent = QStringLiteral("komorebi");
inline const QString kCommandEvent = QStringLiteral("komorebi_command");

JsonPtr jsonFromPayload(const QVariantMap& payload, const QString& key);
// Emits kCommandEvent for a single `komorebic <args>` command.
void sendCommand(EventBus& bus, const QStringList& args);
void sendCommandChain(EventBus& bus, const std::vector<QStringList>& commands);

// The single komorebi client of one Runtime (port of KomorebiEventListener + KomorebiClient
// command calls). Lives on the GUI thread; the pipe listener thread only parses JSON and posts
// results back here.
class KomorebiService : public QObject {
  Q_OBJECT
 public:
  using CommandRunner = std::function<void(const std::vector<QStringList>& commands)>;

  // Default: real komorebic backend, unique `yasb-native-<uuid>` pipe, detached hidden commands.
  explicit KomorebiService(EventBus& bus);
  KomorebiService(EventBus& bus, platform::KomorebiBackend backend, CommandRunner runner,
                  QString pipeName, platform::KomorebiPipeListener::Timing timing);
  ~KomorebiService() override;

  // Stops the listener thread and unsubscribes the pipe; safe to call more than once.
  void stop();
  bool isListening() const { return listener_ && listener_->isRunning(); }
  bool isConnected() const { return connected_; }
  const QString& pipeName() const { return pipeName_; }

  static QString uniquePipeName();
  // `"komorebic.exe" a b && "komorebic.exe" c` (Python's shell=True command chains).
  static QString shellChain(const std::vector<QStringList>& commands);

 private:
  void onBusEvent(const QString& name, const QVariantMap& payload);
  void ensureStarted();
  void deliverConnect(JsonPtr state);
  void deliverUpdate(JsonPtr event, JsonPtr state);
  void deliverDisconnect();

  EventBus& bus_;
  platform::KomorebiBackend backend_;
  CommandRunner runner_;
  QString pipeName_;
  platform::KomorebiPipeListener::Timing timing_;
  std::unique_ptr<platform::KomorebiPipeListener> listener_;
  JsonPtr lastState_;
  bool connected_ = false;
  bool stopped_ = false;
};

}  // namespace komorebi
}  // namespace yasb::widgets

Q_DECLARE_METATYPE(yasb::widgets::komorebi::JsonPtr)

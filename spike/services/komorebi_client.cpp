#include "spike/services/komorebi_client.h"

#include <QDebug>

#include "widgets/komorebi/komorebi_state.h"

namespace yasb::services {

namespace kmb = widgets::komorebi;

namespace {

QString stringOf(const kmb::Json* node, std::string_view key) {
  const kmb::Json* value = kmb::child(node, key);
  return value && value->is_string() ? QString::fromStdString(value->get<std::string>())
                                     : QString();
}

QVariantList toVariant(const kmb::Json& state) {
  QVariantList list;
  const kmb::Json* screens = kmb::elements(&state, "monitors");
  if (!screens || !screens->is_array()) return list;
  int monitorIndex = 0;
  for (const kmb::Json& screen : *screens) {
    const std::optional<int> focused = kmb::focusedWorkspaceIndex(&screen);
    QVariantList workspaces;
    for (int i = 0; i < kmb::workspaceCount(&screen); ++i) {
      const kmb::Json* workspace = kmb::workspaceAt(&screen, i);
      workspaces << QVariantMap{
          {QStringLiteral("index"), i},
          {QStringLiteral("name"), stringOf(workspace, "name")},
          {QStringLiteral("focused"), focused == i},
          {QStringLiteral("hasWindows"), kmb::hasWindows(workspace)},
          {QStringLiteral("windowCount"), kmb::windowCount(workspace, false)}};
    }
    list << QVariantMap{
        {QStringLiteral("index"), monitorIndex++},
        {QStringLiteral("id"),
         static_cast<qlonglong>(kmb::integer(kmb::child(&screen, "id")).value_or(0))},
        {QStringLiteral("name"), stringOf(&screen, "name")},
        {QStringLiteral("workspaces"), workspaces}};
  }
  return list;
}

}  // namespace

Komorebi::Komorebi(QObject* parent) : QObject(parent), service_(bus_) { subscribe(); }

Komorebi::Komorebi(platform::KomorebiBackend backend, kmb::KomorebiService::CommandRunner runner,
                   QString pipeName, platform::KomorebiPipeListener::Timing timing, QObject* parent)
    : QObject(parent),
      service_(bus_, std::move(backend), std::move(runner), std::move(pipeName), timing) {
  subscribe();
}

void Komorebi::subscribe() {
  connect(&bus_, &widgets::EventBus::event, this, &Komorebi::onEvent);
  emit bus_.event(kmb::kSubscribeEvent, {});
}

Komorebi::~Komorebi() { service_.stop(); }

void Komorebi::onEvent(const QString& name, const QVariantMap& payload) {
  if (name != kmb::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("disconnect")) {
      connected_ = false;
      monitors_.clear();
    } else if (kind == QStringLiteral("connect") || kind == QStringLiteral("update")) {
      const kmb::JsonPtr state = kmb::jsonFromPayload(payload, QStringLiteral("state"));
      if (!state) return;
      connected_ = true;
      monitors_ = toVariant(*state);
    } else {
      return;
    }
    emit changed();
  } catch (const std::exception& e) {
    qWarning().noquote() << "komorebi: bad state event:" << e.what();
  }
}

void Komorebi::focusWorkspace(int monitorIndex, int workspaceIndex) {
  kmb::sendCommand(bus_, {QStringLiteral("focus-monitor-workspace"), QString::number(monitorIndex),
                          QString::number(workspaceIndex)});
}

}  // namespace yasb::services

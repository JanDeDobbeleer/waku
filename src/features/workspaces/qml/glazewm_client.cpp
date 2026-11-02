#include "features/workspaces/qml/glazewm_client.h"

#include <QDebug>

namespace waku::workspaces {

namespace gw = glazewm;

namespace {

constexpr auto kDefaultUri = "ws://localhost:6123";

QVariantList toVariant(const std::vector<gw::Monitor>& monitors) {
  QVariantList list;
  for (const gw::Monitor& monitor : monitors) {
    QVariantList workspaces;
    for (const gw::Workspace& workspace : monitor.workspaces) {
      workspaces << QVariantMap{
          {QStringLiteral("name"), workspace.name},
          {QStringLiteral("displayName"),
           workspace.displayName.isEmpty() ? workspace.name : workspace.displayName},
          {QStringLiteral("focused"), workspace.focus},
          {QStringLiteral("displayed"), workspace.isDisplayed},
          {QStringLiteral("windowCount"), workspace.numWindows},
          {QStringLiteral("hasWindows"), workspace.numWindows > 0}};
    }
    list << QVariantMap{{QStringLiteral("name"), monitor.name},
                        {QStringLiteral("hwnd"), static_cast<qlonglong>(monitor.hwnd)},
                        {QStringLiteral("workspaces"), workspaces}};
  }
  return list;
}

}  // namespace

GlazeWM::GlazeWM(QObject* parent) : QObject(parent), service_(bus_) { subscribeAll(); }

GlazeWM::GlazeWM(gw::GlazewmService::Sender sender, gw::GlazewmService::Opener opener,
                 QObject* parent)
    : QObject(parent), service_(bus_, std::move(sender), std::move(opener)) {
  subscribeAll();
}

void GlazeWM::subscribeAll() {
  connect(&bus_, &EventBus::event, this, &GlazeWM::onEvent);
  const QString uri = QString::fromLatin1(kDefaultUri);
  gw::subscribe(bus_, gw::kTopicWorkspaces, uri);
  gw::subscribe(bus_, gw::kTopicTilingDirection, uri);
  gw::subscribe(bus_, gw::kTopicBindingMode, uri);
}

GlazeWM::~GlazeWM() { service_.stop(); }

void GlazeWM::onEvent(const QString& name, const QVariantMap& payload) {
  if (name != gw::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    // Each branch sets `dirty` only when the value differs, so an event that repeats the
    // current state does not wake the bindings.
    bool dirty = false;
    if (kind == QStringLiteral("connection")) {
      const bool connected = payload.value(QStringLiteral("connected")).toBool();
      dirty = connected != connected_;
      connected_ = connected;
    } else if (kind == QStringLiteral("workspaces")) {
      if (const gw::MonitorsPtr monitors = gw::monitorsFromPayload(payload)) {
        QVariantList list = toVariant(*monitors);
        dirty = list != monitors_;
        if (dirty) monitors_ = std::move(list);
      }
    } else if (kind == QStringLiteral("tiling_direction")) {
      const auto direction =
          static_cast<gw::TilingDirection>(payload.value(QStringLiteral("direction")).toInt());
      const QString value = direction == gw::TilingDirection::Vertical
                                ? QStringLiteral("vertical")
                                : QStringLiteral("horizontal");
      dirty = value != tilingDirection_;
      tilingDirection_ = value;
    } else if (kind == QStringLiteral("binding_mode")) {
      if (const gw::BindingModePtr mode = gw::bindingModeFromPayload(payload)) {
        const QString value = mode->displayName.value_or(mode->name.value_or(QString()));
        dirty = value != bindingMode_;
        bindingMode_ = value;
      }
    } else {
      return;
    }
    if (dirty) emit changed();
  } catch (const std::exception& e) {
    qWarning().noquote() << "glazewm: bad state event:" << e.what();
  }
}

void GlazeWM::activateWorkspace(const QString& name) { gw::activateWorkspace(bus_, name); }
void GlazeWM::focusNextWorkspace() { gw::focusNextWorkspace(bus_); }
void GlazeWM::focusPreviousWorkspace() { gw::focusPrevWorkspace(bus_); }
void GlazeWM::toggleTilingDirection() { gw::toggleTilingDirection(bus_); }
void GlazeWM::enableBindingMode(const QString& name) { gw::enableBindingMode(bus_, name); }
void GlazeWM::disableBindingMode(const QString& name) { gw::disableBindingMode(bus_, name); }

}  // namespace waku::workspaces

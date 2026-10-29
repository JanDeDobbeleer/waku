#include "spike/services/glazewm_client.h"

#include <QDebug>

namespace wabar::services {

namespace gw = widgets::glazewm;

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
  connect(&bus_, &widgets::EventBus::event, this, &GlazeWM::onEvent);
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
    if (kind == QStringLiteral("connection")) {
      connected_ = payload.value(QStringLiteral("connected")).toBool();
    } else if (kind == QStringLiteral("workspaces")) {
      if (const gw::MonitorsPtr monitors = gw::monitorsFromPayload(payload)) {
        monitors_ = toVariant(*monitors);
      }
    } else if (kind == QStringLiteral("tiling_direction")) {
      const auto direction =
          static_cast<gw::TilingDirection>(payload.value(QStringLiteral("direction")).toInt());
      tilingDirection_ = direction == gw::TilingDirection::Vertical ? QStringLiteral("vertical")
                                                                    : QStringLiteral("horizontal");
    } else if (kind == QStringLiteral("binding_mode")) {
      if (const gw::BindingModePtr mode = gw::bindingModeFromPayload(payload)) {
        bindingMode_ = mode->displayName.value_or(mode->name.value_or(QString()));
      }
    } else {
      return;
    }
    emit changed();
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

}  // namespace wabar::services

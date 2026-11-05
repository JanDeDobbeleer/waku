#include "features/workspaces/qml/workspaces_service.h"

#include <QQmlEngine>

namespace waku::workspaces {

namespace {

QVariantMap workspace(const QString& id, const QString& label, bool focused, bool active,
                      bool hasWindows, int windowCount, const QVariantList& windows) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("label"), label},
          {QStringLiteral("focused"), focused},
          {QStringLiteral("active"), active},
          {QStringLiteral("hasWindows"), hasWindows},
          {QStringLiteral("windowCount"), windowCount},
          {QStringLiteral("windows"), windows}};
}

QVariantMap monitor(qlonglong handle, const QString& name, const QVariantList& workspaces) {
  return {{QStringLiteral("handle"), handle},
          {QStringLiteral("name"), name},
          {QStringLiteral("workspaces"), workspaces}};
}

// Virtual desktops are not tied to a monitor, so there is one entry with handle 0.
QVariantList fromWindows(const QVariantList& desktops) {
  QVariantList workspaces;
  for (const QVariant& d : desktops) {
    const QVariantMap desktop = d.toMap();
    const bool current = desktop.value(QStringLiteral("current")).toBool();
    const QVariantList windows = desktop.value(QStringLiteral("windows")).toList();
    workspaces << workspace(QString::number(desktop.value(QStringLiteral("index")).toInt()),
                            desktop.value(QStringLiteral("label")).toString(), current, current,
                            !windows.isEmpty(), static_cast<int>(windows.size()), windows);
  }
  return {monitor(0, QString(), workspaces)};
}

}  // namespace

Workspaces::Workspaces(VirtualDesktops* desktops, QObject* parent)
    : QObject(parent), desktops_(desktops) {
  if (desktops) connect(desktops, &VirtualDesktops::changed, this, &Workspaces::rebuild);
  update();
}

Workspaces* Workspaces::create(QQmlEngine* engine, QJSEngine*) {
  auto* desktops = engine->singletonInstance<VirtualDesktops*>("waku.services", "VirtualDesktops");
  return new Workspaces(desktops);
}

void Workspaces::rebuild() {
  if (update()) emit changed();
}

bool Workspaces::update() {
  const bool up = desktops_ && desktops_->available();
  const QString provider = up ? QStringLiteral("windows") : QString();
  QVariantList monitors;
  if (up) monitors = fromWindows(desktops_->desktops());
  // Nothing moved on screen when the model is as it was; the widgets then keep their delegates.
  const bool canRemove = up && desktops_->canRemove();
  if (provider == provider_ && monitors == monitors_ && canRemove == canRemove_) return false;
  canRemove_ = canRemove;
  provider_ = provider;
  monitors_ = std::move(monitors);
  return true;
}

void Workspaces::remove(qlonglong, const QString& id) {
  if (!desktops_) return;
  bool ok = false;
  const int index = id.toInt(&ok);
  if (ok) desktops_->remove(index);
}

void Workspaces::focus(qlonglong, const QString& id) {
  if (!desktops_) return;
  bool ok = false;
  const int index = id.toInt(&ok);
  if (ok) desktops_->switchTo(index);
}

void Workspaces::step(qlonglong monitorHandle, int direction) {
  for (const QVariant& m : monitors_) {
    const QVariantMap map = m.toMap();
    const qlonglong handle = map.value(QStringLiteral("handle")).toLongLong();
    if (handle != monitorHandle && handle != 0) continue;
    const QVariantList workspaces = map.value(QStringLiteral("workspaces")).toList();
    const int count = workspaces.size();
    if (count == 0) return;
    int current = 0;
    for (int i = 0; i < count; ++i) {
      if (workspaces[i].toMap().value(QStringLiteral("active")).toBool()) current = i;
    }
    const int next = ((current + direction) % count + count) % count;
    focus(monitorHandle, workspaces[next].toMap().value(QStringLiteral("id")).toString());
    return;
  }
}

}  // namespace waku::workspaces

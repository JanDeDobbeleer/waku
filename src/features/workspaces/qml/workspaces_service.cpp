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

QVariantList fromKomorebi(const QVariantList& monitors) {
  QVariantList out;
  for (const QVariant& m : monitors) {
    const QVariantMap map = m.toMap();
    const bool monitorFocused = map.value(QStringLiteral("focused")).toBool();
    QVariantList workspaces;
    for (const QVariant& w : map.value(QStringLiteral("workspaces")).toList()) {
      const QVariantMap ws = w.toMap();
      const int index = ws.value(QStringLiteral("index")).toInt();
      const QString name = ws.value(QStringLiteral("name")).toString();
      const bool active = ws.value(QStringLiteral("focused")).toBool();
      workspaces << workspace(
          QString::number(index), name.isEmpty() ? QString::number(index + 1) : name,
          active && monitorFocused, active, ws.value(QStringLiteral("hasWindows")).toBool(),
          ws.value(QStringLiteral("windowCount")).toInt(),
          ws.value(QStringLiteral("windows")).toList());
    }
    out << monitor(map.value(QStringLiteral("id")).toLongLong(),
                   map.value(QStringLiteral("name")).toString(), workspaces);
  }
  return out;
}

QVariantList fromGlazeWM(const QVariantList& monitors) {
  QVariantList out;
  for (const QVariant& m : monitors) {
    const QVariantMap map = m.toMap();
    QVariantList workspaces;
    for (const QVariant& w : map.value(QStringLiteral("workspaces")).toList()) {
      const QVariantMap ws = w.toMap();
      workspaces << workspace(ws.value(QStringLiteral("name")).toString(),
                              ws.value(QStringLiteral("displayName")).toString(),
                              ws.value(QStringLiteral("focused")).toBool(),
                              ws.value(QStringLiteral("displayed")).toBool(),
                              ws.value(QStringLiteral("hasWindows")).toBool(),
                              ws.value(QStringLiteral("windowCount")).toInt(), QVariantList());
    }
    out << monitor(map.value(QStringLiteral("hwnd")).toLongLong(),
                   map.value(QStringLiteral("name")).toString(), workspaces);
  }
  return out;
}

// Virtual desktops are not tied to a monitor, so there is one entry with handle 0.
QVariantList fromWindows(const QVariantList& desktops) {
  QVariantList workspaces;
  for (const QVariant& d : desktops) {
    const QVariantMap desktop = d.toMap();
    const bool current = desktop.value(QStringLiteral("current")).toBool();
    const int windowCount = desktop.value(QStringLiteral("windowCount")).toInt();
    workspaces << workspace(QString::number(desktop.value(QStringLiteral("index")).toInt()),
                            desktop.value(QStringLiteral("label")).toString(), current, current,
                            windowCount > 0, windowCount, QVariantList());
  }
  return {monitor(0, QString(), workspaces)};
}

}  // namespace

Workspaces::Workspaces(Komorebi* komorebi, GlazeWM* glazewm, VirtualDesktops* desktops,
                       QObject* parent)
    : QObject(parent), komorebi_(komorebi), glazewm_(glazewm), desktops_(desktops) {
  if (komorebi) connect(komorebi, &Komorebi::changed, this, &Workspaces::rebuild);
  if (glazewm) connect(glazewm, &GlazeWM::changed, this, &Workspaces::rebuild);
  if (desktops) connect(desktops, &VirtualDesktops::changed, this, &Workspaces::rebuild);
  update();
}

Workspaces* Workspaces::create(QQmlEngine* engine, QJSEngine*) {
  auto* komorebi = engine->singletonInstance<Komorebi*>("waku.services", "Komorebi");
  auto* glazewm = engine->singletonInstance<GlazeWM*>("waku.services", "GlazeWM");
  auto* desktops = engine->singletonInstance<VirtualDesktops*>("waku.services", "VirtualDesktops");
  return new Workspaces(komorebi, glazewm, desktops);
}

void Workspaces::setPrefer(const QString& prefer) {
  if (prefer == prefer_) return;
  prefer_ = prefer;
  update();
  emit changed();  // `prefer` notifies through changed()
}

void Workspaces::rebuild() {
  if (update()) emit changed();
}

bool Workspaces::update() {
  const bool komorebiUp = komorebi_ && komorebi_->connected();
  const bool glazewmUp = glazewm_ && glazewm_->connected();
  const bool windowsUp = desktops_ && desktops_->available();
  QString provider;
  if (windowsUp && (prefer_ == QStringLiteral("windows") || (!komorebiUp && !glazewmUp))) {
    provider = QStringLiteral("windows");
  } else if (komorebiUp && glazewmUp) {
    provider = prefer_ == QStringLiteral("glazewm") ? QStringLiteral("glazewm")
                                                    : QStringLiteral("komorebi");
  } else if (komorebiUp) {
    provider = QStringLiteral("komorebi");
  } else if (glazewmUp) {
    provider = QStringLiteral("glazewm");
  }

  QVariantList monitors;
  if (provider == QStringLiteral("komorebi")) {
    monitors = fromKomorebi(komorebi_->monitors());
  } else if (provider == QStringLiteral("glazewm")) {
    monitors = fromGlazeWM(glazewm_->monitors());
  } else if (provider == QStringLiteral("windows")) {
    monitors = fromWindows(desktops_->desktops());
  }
  // Nothing moved on screen when the model is as it was; the widgets then keep their delegates.
  if (provider == provider_ && monitors == monitors_) return false;
  provider_ = provider;
  monitors_ = std::move(monitors);
  return true;
}

void Workspaces::focus(qlonglong monitorHandle, const QString& id) {
  if (provider_ == QStringLiteral("windows")) {
    bool ok = false;
    const int index = id.toInt(&ok);
    if (desktops_ && ok) desktops_->switchTo(index);  // every monitor shows the same desktops
    return;
  }
  if (provider_ == QStringLiteral("komorebi")) {
    if (!komorebi_) return;
    for (const QVariant& m : komorebi_->monitors()) {
      const QVariantMap map = m.toMap();
      if (map.value(QStringLiteral("id")).toLongLong() != monitorHandle) continue;
      bool ok = false;
      const int index = id.toInt(&ok);
      if (ok) komorebi_->focusWorkspace(map.value(QStringLiteral("index")).toInt(), index);
      return;
    }
  } else if (provider_ == QStringLiteral("glazewm")) {
    if (!glazewm_) return;
    for (const QVariant& m : monitors_) {
      if (m.toMap().value(QStringLiteral("handle")).toLongLong() == monitorHandle) {
        glazewm_->activateWorkspace(id);
        return;
      }
    }
  }
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

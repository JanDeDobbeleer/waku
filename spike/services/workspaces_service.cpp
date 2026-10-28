#include "spike/services/workspaces_service.h"

#include <QQmlEngine>

namespace tidy::services {

namespace {

QVariantMap workspace(const QString& id, const QString& label, bool focused, bool active,
                      bool hasWindows, int windowCount) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("label"), label},
          {QStringLiteral("focused"), focused},
          {QStringLiteral("active"), active},
          {QStringLiteral("hasWindows"), hasWindows},
          {QStringLiteral("windowCount"), windowCount}};
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
          ws.value(QStringLiteral("windowCount")).toInt());
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
                              ws.value(QStringLiteral("windowCount")).toInt());
    }
    out << monitor(map.value(QStringLiteral("hwnd")).toLongLong(),
                   map.value(QStringLiteral("name")).toString(), workspaces);
  }
  return out;
}

}  // namespace

Workspaces::Workspaces(Komorebi* komorebi, GlazeWM* glazewm, QObject* parent)
    : QObject(parent), komorebi_(komorebi), glazewm_(glazewm) {
  if (komorebi_) connect(komorebi_, &Komorebi::changed, this, &Workspaces::rebuild);
  if (glazewm_) connect(glazewm_, &GlazeWM::changed, this, &Workspaces::rebuild);
  rebuild();
}

Workspaces* Workspaces::create(QQmlEngine* engine, QJSEngine*) {
  auto* komorebi = engine->singletonInstance<Komorebi*>("tidy.services", "Komorebi");
  auto* glazewm = engine->singletonInstance<GlazeWM*>("tidy.services", "GlazeWM");
  return new Workspaces(komorebi, glazewm);
}

void Workspaces::setPrefer(const QString& prefer) {
  if (prefer == prefer_) return;
  prefer_ = prefer;
  rebuild();
}

void Workspaces::rebuild() {
  const bool komorebiUp = komorebi_ && komorebi_->connected();
  const bool glazewmUp = glazewm_ && glazewm_->connected();
  if (komorebiUp && glazewmUp) {
    provider_ = prefer_ == QStringLiteral("glazewm") ? QStringLiteral("glazewm")
                                                     : QStringLiteral("komorebi");
  } else if (komorebiUp) {
    provider_ = QStringLiteral("komorebi");
  } else if (glazewmUp) {
    provider_ = QStringLiteral("glazewm");
  } else {
    provider_.clear();
  }

  if (provider_ == QStringLiteral("komorebi")) {
    monitors_ = fromKomorebi(komorebi_->monitors());
  } else if (provider_ == QStringLiteral("glazewm")) {
    monitors_ = fromGlazeWM(glazewm_->monitors());
  } else {
    monitors_.clear();
  }
  emit changed();
}

void Workspaces::focus(qlonglong monitorHandle, const QString& id) {
  if (provider_ == QStringLiteral("komorebi")) {
    for (const QVariant& m : komorebi_->monitors()) {
      const QVariantMap map = m.toMap();
      if (map.value(QStringLiteral("id")).toLongLong() != monitorHandle) continue;
      bool ok = false;
      const int index = id.toInt(&ok);
      if (ok) komorebi_->focusWorkspace(map.value(QStringLiteral("index")).toInt(), index);
      return;
    }
  } else if (provider_ == QStringLiteral("glazewm")) {
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
    if (map.value(QStringLiteral("handle")).toLongLong() != monitorHandle) continue;
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

}  // namespace tidy::services

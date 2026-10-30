#include "host/quickshell/systemtray/qml/system_tray_item.h"

#include <QFileInfo>

namespace wabar::quickshell {

QString SystemTrayItem::idFromExe(const QString& exe) {
  return QFileInfo(QString(exe).replace(QLatin1Char('\\'), QLatin1Char('/')))
      .completeBaseName()
      .toLower();
}

void SystemTrayItem::setRow(const QVariantMap& row) {
  const int trayId = row.value(QStringLiteral("id"), -1).toInt();
  QString id = idFromExe(row.value(QStringLiteral("exe")).toString());
  if (id.isEmpty()) id = QString::number(trayId);
  const QString tooltip = row.value(QStringLiteral("tooltip")).toString();
  const QString title = tooltip.isEmpty() ? id : tooltip;
  const QString icon = row.value(QStringLiteral("iconUrl")).toString();
  const bool differs =
      id != id_ || title != title_ || icon != icon_ || tooltip != tooltip_ || trayId != trayId_;
  id_ = id;
  title_ = title;
  icon_ = icon;
  tooltip_ = tooltip;
  trayId_ = trayId;
  if (differs) emit changed();
  if (!ready_) {
    ready_ = true;
    emit ready();
  }
}

void SystemTrayItem::display(QObject* parentWindow, qreal relativeX, qreal relativeY) {
  Q_UNUSED(parentWindow);
  Q_UNUSED(relativeX);
  Q_UNUSED(relativeY);
  emit displayRequested();
}

}  // namespace wabar::quickshell

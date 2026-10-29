#include "kernel/qt/app_data.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace wabar::kernel {

namespace {

const QString kProductFolder = QStringLiteral("wabar");
const QString kLegacyFolder = QStringLiteral("YASB");
const QString kMarkerFile = QStringLiteral("migrated_from_yasb");

QString dirOf(const QString& base, const QString& folder) { return QDir(base).filePath(folder); }

QStringList legacyFileNames(const QDir& legacy) {
  QStringList names = {QStringLiteral("taskbar_pinned.json"), QStringLiteral("github_token")};
  const QStringList trayState =
      legacy.entryList({QStringLiteral("systray_state_*.json")}, QDir::Files, QDir::Name);
  return names + trayState;
}

}  // namespace

QString localAppDataBase() {
  const QString base = qEnvironmentVariable("LOCALAPPDATA");
  return base.isEmpty() ? QDir::homePath() + QStringLiteral("/AppData/Local") : base;
}

AppDataMigration migrateLegacyAppData(const QString& base) {
  AppDataMigration result;
  const QDir legacy(dirOf(base, kLegacyFolder));
  if (!legacy.exists()) return result;
  const QDir target(dirOf(base, kProductFolder));
  if (target.exists(kMarkerFile)) return result;
  if (!QDir().mkpath(target.path())) return result;

  result.ran = true;
  for (const QString& name : legacyFileNames(legacy)) {
    if (!legacy.exists(name)) continue;
    if (target.exists(name)) {
      result.skipped << name;
      continue;
    }
    // QFile::copy refuses to overwrite, so even a file created since the exists() test survives.
    if (QFile::copy(legacy.filePath(name), target.filePath(name)))
      result.copied << name;
    else
      qWarning().noquote() << "wabar: could not copy" << legacy.filePath(name) << "to"
                           << target.filePath(name);
  }
  QFile marker(target.filePath(kMarkerFile));
  if (marker.open(QIODevice::WriteOnly)) marker.write("copied once from the YASB folder\n");
  if (!result.copied.isEmpty())
    qInfo().noquote() << "wabar: copied" << result.copied.join(QStringLiteral(", ")) << "from"
                      << QDir::toNativeSeparators(legacy.path()) << "to"
                      << QDir::toNativeSeparators(target.path());
  return result;
}

QString appDataDir() {
  static QMutex mutex;
  static QSet<QString> migrated;
  const QString base = localAppDataBase();
  const QString dir = dirOf(base, kProductFolder);
  const QMutexLocker lock(&mutex);
  QDir().mkpath(dir);
  if (!migrated.contains(base)) {
    migrated.insert(base);
    migrateLegacyAppData(base);
  }
  return dir;
}

}  // namespace wabar::kernel

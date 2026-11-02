#include "control/config_init.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace waku::control {

QString starterShellText(const QString& templateText) {
  const QString header = QStringLiteral(
      "// The starter shell, written by `wakuc config init`. Edit this file and save it: waku\n"
      "// reloads the bar on every save, and a mistake keeps the previous shell running.\n"
      "// `wakuc reload` reloads by hand. Notes on writing shells:\n"
      "// https://github.com/JanDeDobbeleer/waku/blob/main/docs/shells.md\n");
  // Skip the leading comment block of the template.
  qsizetype pos = 0;
  while (templateText.mid(pos, 2) == QLatin1String("//")) {
    const qsizetype eol = templateText.indexOf(QLatin1Char('\n'), pos);
    if (eol < 0) {
      pos = templateText.size();
      break;
    }
    pos = eol + 1;
  }
  return header + templateText.mid(pos);
}

ConfigInitResult initConfigHome(const QString& configHome, const QString& shellText, bool force) {
  ConfigInitResult result;
  const QDir dir(QDir::cleanPath(QDir(configHome).absolutePath()));
  const QString path = dir.filePath(QStringLiteral("shell.qml"));
  result.path = QDir::toNativeSeparators(path);
  if (QFileInfo::exists(path) && !force) {
    result.status = ConfigInitResult::AlreadyExists;
    result.message =
        QStringLiteral("wakuc: %1 already exists (use --force to overwrite)").arg(result.path);
    return result;
  }
  if (!QDir().mkpath(dir.path())) {
    result.message =
        QStringLiteral("wakuc: cannot create %1").arg(QDir::toNativeSeparators(dir.path()));
    return result;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(shellText.toUtf8()) < 0 || !file.commit()) {
    result.message =
        QStringLiteral("wakuc: cannot write %1: %2").arg(result.path, file.errorString());
    return result;
  }
  result.status = ConfigInitResult::Written;
  return result;
}

}  // namespace waku::control

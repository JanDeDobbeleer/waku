#include "features/apps/catalog.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace wabar::apps {

bool isNoiseName(const QString& name) {
  const QString lower = name.toLower();
  if (lower.contains(QLatin1String("uninstall")) || lower.contains(QLatin1String("readme")))
    return true;
  static const QRegularExpression help(QStringLiteral("\\bhelp\\b"));
  return lower.contains(help);
}

namespace {

QString trailingFolders(const QString& relativePath) {
  const qsizetype slash = relativePath.lastIndexOf(QLatin1Char('/'));
  return slash < 0 ? QString() : relativePath.left(slash);
}

}  // namespace

std::vector<AppRecord> scanShortcuts(const QStringList& folders, const ShortcutResolver& resolver) {
  std::vector<AppRecord> result;
  if (!resolver) return result;
  QSet<QString> seen;
  for (const QString& folder : folders) {
    const QDir root(folder);
    if (!root.exists()) continue;
    QStringList files;
    // QDir::System: Qt lists a Windows .lnk file as a link, and only with this flag. It then does
    // not apply its hidden filter to them; the real resolver rejects hidden shortcuts.
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString file = it.next();
      if (file.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) files.append(file);
    }
    files.sort(Qt::CaseInsensitive);
    for (const QString& file : files) {
      QString id = root.relativeFilePath(file);
      id.chop(4);  // ".lnk"
      id.replace(QLatin1Char('\\'), QLatin1Char('/'));
      const QString name = QFileInfo(file).completeBaseName();
      if (name.isEmpty() || isNoiseName(name)) continue;
      const QString key = id.toLower();
      if (seen.contains(key)) continue;
      const ShortcutInfo info = resolver(file);
      if (!info.ok) continue;
      seen.insert(key);

      AppRecord record;
      record.id = id;
      record.name = name;
      record.comment = info.description;
      record.kind = AppKind::Shortcut;
      record.path = QDir::toNativeSeparators(file);
      record.target = info.target;
      record.arguments = info.arguments;
      record.workingDirectory = info.workingDirectory;
      record.iconSource = record.path;
      const QString categories = trailingFolders(id);
      if (!categories.isEmpty()) record.categories = categories.split(QLatin1Char('/'));
      const QString exe = startupClassFor(record);
      if (!exe.isEmpty()) record.keywords.append(exe);
      result.push_back(std::move(record));
    }
  }
  return result;
}

std::vector<AppRecord> buildCatalog(std::vector<AppRecord> shortcuts,
                                    std::vector<AppRecord> storeApps) {
  QSet<QString> names;
  QSet<QString> ids;
  std::vector<AppRecord> result;
  result.reserve(shortcuts.size() + storeApps.size());
  for (AppRecord& record : shortcuts) {
    if (record.id.isEmpty() || record.name.isEmpty()) continue;
    if (!ids.contains(record.id.toLower())) {
      ids.insert(record.id.toLower());
      names.insert(record.name.toLower());
      result.push_back(std::move(record));
    }
  }
  for (AppRecord& record : storeApps) {
    if (record.id.isEmpty() || record.name.isEmpty() || isNoiseName(record.name)) continue;
    if (ids.contains(record.id.toLower()) || names.contains(record.name.toLower())) continue;
    ids.insert(record.id.toLower());
    result.push_back(std::move(record));
  }
  std::sort(result.begin(), result.end(), [](const AppRecord& a, const AppRecord& b) {
    const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
    return byName != 0 ? byName < 0 : a.id < b.id;
  });
  return result;
}

QStringList watchedDirectories(const QStringList& folders) {
  QStringList result;
  for (const QString& folder : folders) {
    const QDir root(folder);
    if (!root.exists()) continue;
    result.append(root.absolutePath());
    QDirIterator it(root.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) result.append(it.next());
  }
  result.removeDuplicates();
  return result;
}

QString execStringFor(const AppRecord& record) {
  if (record.kind == AppKind::Store) return QStringLiteral("shell:AppsFolder\\") + record.aumid;
  if (record.target.isEmpty()) return QStringLiteral("\"") + record.path + QStringLiteral("\"");
  QString text = QStringLiteral("\"") + record.target + QStringLiteral("\"");
  if (!record.arguments.isEmpty()) text += QLatin1Char(' ') + record.arguments;
  return text;
}

QStringList commandFor(const AppRecord& record) {
  if (record.kind == AppKind::Store)
    return {QStringLiteral("explorer.exe"), QStringLiteral("shell:AppsFolder\\") + record.aumid};
  if (record.target.isEmpty()) return {record.path};
  QStringList command{record.target};
  if (!record.arguments.isEmpty()) command += QProcess::splitCommand(record.arguments);
  return command;
}

QString startupClassFor(const AppRecord& record) {
  if (record.target.isEmpty()) return QString();
  return QFileInfo(record.target).completeBaseName();
}

}  // namespace wabar::apps

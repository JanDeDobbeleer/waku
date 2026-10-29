// Media source app names (port of services/media/source_apps.py). Name resolution runs on the
// media worker thread. The process, window and shell-app lookups live in kernel/win32.
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/base.h>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <optional>

#include "features/media/media_session.h"
#include "kernel/win32/app_activation.h"

namespace wabar::media {
namespace {

QString baseName(const QString& path) {
  const qsizetype slash = std::max(path.lastIndexOf(u'\\'), path.lastIndexOf(u'/'));
  return slash >= 0 ? path.mid(slash + 1) : path;
}

QString stem(const QString& file) {
  const qsizetype dot = file.lastIndexOf(u'.');
  return dot > 0 ? file.left(dot) : file;
}

QString appInfoName(const QString& aumid) {
  try {
    const auto info =
        winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(aumid.toStdWString());
    if (!info) return {};
    return QString::fromStdWString(std::wstring(info.DisplayInfo().DisplayName())).trimmed();
  } catch (...) {
    return {};
  }
}

QString nameFromProcess(const QString& aumid) {
  const QString process = wabar::kernel::exeNameForAumid(aumid);
  if (process.isEmpty()) return {};
  const QString name = wabar::kernel::appNameForExe(process);
  return name.isEmpty() ? stem(process) : name;
}

QString humanize(const QString& aumid) {
  if (aumid.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) return stem(baseName(aumid));
  if (aumid.contains(u'!')) return aumid.section(u'!', -1);
  QString last = aumid.contains(u'.') ? aumid.section(u'.', -1) : aumid;
  return last.replace(u'-', u' ').replace(u'_', u' ');
}

}  // namespace

QString resolveSourceAppName(const QString& aumid) {
  if (aumid.isEmpty()) return {};
  static QMutex mutex;
  static QHash<QString, QString> cache;
  {
    QMutexLocker lock(&mutex);
    if (auto it = cache.constFind(aumid); it != cache.constEnd()) return *it;
  }
  QString name = appInfoName(aumid);
  if (name.isEmpty() && !aumid.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
    name = wabar::kernel::resolveShellAppByAumid(aumid).name;
  if (name.isEmpty()) name = nameFromProcess(aumid);
  if (name.isEmpty()) name = wabar::kernel::appNameForWindowAumid(aumid);
  if (name.isEmpty()) name = humanize(aumid);
  QMutexLocker lock(&mutex);
  cache.insert(aumid, name);
  return name;
}

QString sourceAppClassName(const QString& displayName) {
  return displayName.toLower().replace(u' ', u'-');
}

}  // namespace wabar::media

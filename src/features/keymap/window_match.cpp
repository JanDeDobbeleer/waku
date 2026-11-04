#include "features/keymap/window_match.h"

namespace waku::keymap {
namespace {

QString withoutExe(const QString& name) {
  QString lower = name.trimmed().toLower();
  if (lower.endsWith(QLatin1String(".exe"))) lower.chop(4);
  return lower;
}

}  // namespace

bool processNamesMatch(const QString& name, const QString& process) {
  const QString wanted = withoutExe(name);
  return !wanted.isEmpty() && wanted == withoutExe(process);
}

bool windowMatchesTarget(const QString& target, const QString& processName, const QString& title) {
  if (processNamesMatch(target, processName)) return true;
  const QString needle = target.trimmed();
  return !needle.isEmpty() && title.contains(needle, Qt::CaseInsensitive);
}

}  // namespace waku::keymap

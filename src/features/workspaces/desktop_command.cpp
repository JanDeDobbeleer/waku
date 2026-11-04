#include "features/workspaces/desktop_command.h"

namespace waku::workspaces {
namespace {

QString usage() {
  return QStringLiteral("error: usage: desktop goto <n> | move <n> | next | previous | status");
}

// A desktop number: digits only, 1 or more.
bool parseNumber(const QString& text, int* out) {
  bool ok = false;
  const int value = text.toInt(&ok);
  if (!ok || value < 1 || text.startsWith(QLatin1Char('+'))) return false;
  *out = value;
  return true;
}

}  // namespace

QString answerDesktopCommand(const QStringList& words, DesktopControl* desktops) {
  if (words.isEmpty()) return usage();
  if (!desktops) return QStringLiteral("error: virtual desktops are not available");
  const QString verb = words.first().toLower();
  const qsizetype count = words.size();
  int number = 0;
  if (verb == QLatin1String("goto") && count == 2 && parseNumber(words.at(1), &number)) {
    return desktops->controlGoto(number);
  }
  if (verb == QLatin1String("move") && count == 2 && parseNumber(words.at(1), &number)) {
    return desktops->controlMove(number);
  }
  if (verb == QLatin1String("next") && count == 1) return desktops->controlStep(1);
  if (verb == QLatin1String("previous") && count == 1) return desktops->controlStep(-1);
  if (verb == QLatin1String("status") && count == 1) return desktops->controlStatus();
  return usage();
}

}  // namespace waku::workspaces

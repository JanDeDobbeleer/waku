#include "platform/command_line.h"

namespace yasb::platform {

QString quoteCmdLine(const QStringList& argv) {
  QStringList parts;
  for (const QString& arg : argv) {
    const bool needQuote =
        arg.isEmpty() || arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t'));
    QString result;
    int backslashes = 0;
    if (needQuote) result += QLatin1Char('"');
    for (const QChar c : arg) {
      if (c == QLatin1Char('\\')) {
        ++backslashes;
        continue;
      }
      if (c == QLatin1Char('"')) {
        result += QString(backslashes * 2 + 1, QLatin1Char('\\')) + QLatin1Char('"');
      } else {
        result += QString(backslashes, QLatin1Char('\\')) + c;
      }
      backslashes = 0;
    }
    if (backslashes) result += QString(backslashes, QLatin1Char('\\'));
    if (needQuote) {
      result += QString(backslashes, QLatin1Char('\\')) + QLatin1Char('"');
    }
    parts << result;
  }
  return parts.join(QLatin1Char(' '));
}

}  // namespace yasb::platform

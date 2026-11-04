#include "app/wakukeys/keys_args.h"

namespace wakukeys {

QString keysUsage() {
  return QStringLiteral(
      "usage: wakukeys [--config <file>] [--config-home <dir>] [--log-file <file>] | "
      "wakukeys reload|stop|status");
}

KeysArgs parseKeysArgs(const QStringList& arguments) {
  KeysArgs result;
  bool optionSet = false;
  const auto fail = [&result](const QString& text) {
    result.error = text + QLatin1Char('\n') + keysUsage();
    return result;
  };
  for (qsizetype i = 0; i < arguments.size(); ++i) {
    const QString& argument = arguments.at(i);
    if (argument.startsWith(QLatin1Char('-'))) {
      const qsizetype equals = argument.indexOf(QLatin1Char('='));
      const QString name = equals < 0 ? argument : argument.left(equals);
      QString* target = name == QLatin1String("--config")        ? &result.configFile
                        : name == QLatin1String("--config-home") ? &result.configHome
                        : name == QLatin1String("--log-file")    ? &result.logFile
                                                                 : nullptr;
      if (target == nullptr)
        return fail(QStringLiteral("wakukeys: unknown option %1").arg(argument));
      if (equals >= 0) {
        *target = argument.mid(equals + 1);
      } else if (i + 1 < arguments.size()) {
        *target = arguments.at(++i);
      }
      if (target->isEmpty()) return fail(QStringLiteral("wakukeys: %1 needs a value").arg(name));
      optionSet = true;
      continue;
    }
    const bool known = argument == QLatin1String("reload") || argument == QLatin1String("stop") ||
                       argument == QLatin1String("status");
    if (!known || !result.verb.isEmpty())
      return fail(QStringLiteral("wakukeys: unexpected %1").arg(argument));
    result.verb = argument;
  }
  if (!result.verb.isEmpty() && optionSet) {
    return fail(QStringLiteral("wakukeys: reload, stop and status take no options"));
  }
  return result;
}

}  // namespace wakukeys

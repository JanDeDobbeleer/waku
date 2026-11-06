#include "features/keymap/keymap_action.h"

#include <QByteArray>
#include <QDir>

#include "control/control.h"
#include "features/keymap/window_match.h"

namespace waku::keymap {
namespace {

constexpr QLatin1String kPrefix("waku:");

ActionParse fail(const QString& message) {
  ActionParse result;
  result.error = message;
  return result;
}

ActionParse ok(Action action) {
  ActionParse result;
  result.action = std::move(action);
  return result;
}

Action barAction(const QStringList& words) {
  Action action;
  action.kind = Action::Kind::Bar;
  action.text = control::joinCommand(words);
  return action;
}

// The first word of `text` (bare, or in double quotes with a doubled quote for a quote) and the
// trimmed rest. False for an unterminated quote or no word.
bool takeWord(const QString& text, QString* word, QString* rest) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return false;
  if (trimmed.at(0) != QLatin1Char('"')) {
    qsizetype end = 0;
    while (end < trimmed.size() && !trimmed.at(end).isSpace()) ++end;
    *word = trimmed.left(end);
    *rest = trimmed.mid(end).trimmed();
    return true;
  }
  QString value;
  qsizetype i = 1;
  while (i < trimmed.size()) {
    const QChar c = trimmed.at(i);
    if (c == QLatin1Char('"')) {
      if (i + 1 < trimmed.size() && trimmed.at(i + 1) == QLatin1Char('"')) {
        value += c;
        i += 2;
        continue;
      }
      *word = value;
      *rest = trimmed.mid(i + 1).trimmed();
      return true;
    }
    value += c;
    ++i;
  }
  return false;
}

}  // namespace

ActionParse parseAction(const QString& command) {
  const QString text = command.trimmed();
  if (text.isEmpty()) return fail(QStringLiteral("empty command"));
  if (!text.startsWith(kPrefix, Qt::CaseInsensitive)) {
    Action action;
    action.kind = Action::Kind::Shell;
    action.text = text;
    return ok(action);
  }
  const QString body = text.mid(kPrefix.size()).trimmed();
  qsizetype verbEnd = 0;
  while (verbEnd < body.size() && !body.at(verbEnd).isSpace()) ++verbEnd;
  const QString verb = body.left(verbEnd).toLower();
  const QString args = body.mid(verbEnd).trimmed();
  const QString prefixed = QStringLiteral("waku:") + verb;

  if (verb == QLatin1String("activate-or-launch")) {
    QString target;
    QString launch;
    if (!takeWord(args, &target, &launch) || target.isEmpty() || launch.isEmpty()) {
      return fail(QStringLiteral("%1 needs an executable or title and a command").arg(prefixed));
    }
    Action action;
    action.kind = Action::Kind::ActivateOrLaunch;
    action.target = target;
    action.text = launch;
    return ok(action);
  }

  QStringList words;
  if (!control::splitCommand(args, &words)) {
    return fail(QStringLiteral("%1: unterminated quote").arg(prefixed));
  }
  if (verb == QLatin1String("shortcut")) {
    if (words.size() != 1) return fail(QStringLiteral("waku:shortcut needs one name"));
    return ok(barAction({QStringLiteral("shortcut"), words.first()}));
  }
  if (verb == QLatin1String("surface")) {
    const QString what = words.value(0).toLower();
    if (words.size() != 2 || (what != QLatin1String("show") && what != QLatin1String("hide") &&
                              what != QLatin1String("toggle"))) {
      return fail(QStringLiteral("waku:surface needs show, hide or toggle and a name"));
    }
    return ok(barAction({QStringLiteral("surface"), what, words.at(1)}));
  }
  if (verb == QLatin1String("call")) {
    if (words.size() < 2) return fail(QStringLiteral("waku:call needs a target and a function"));
    return ok(barAction(QStringList{QStringLiteral("call")} + words));
  }
  if (verb == QLatin1String("reload") || verb == QLatin1String("keys-reload") ||
      verb == QLatin1String("keys-stop")) {
    if (!words.isEmpty()) return fail(QStringLiteral("%1 takes no arguments").arg(prefixed));
    Action action;
    action.kind = verb == QLatin1String("reload")        ? Action::Kind::Bar
                  : verb == QLatin1String("keys-reload") ? Action::Kind::KeysReload
                                                         : Action::Kind::KeysStop;
    if (action.kind == Action::Kind::Bar) action.text = QStringLiteral("reload");
    return ok(action);
  }
  return fail(QStringLiteral("unknown action '%1'").arg(prefixed));
}

ShellInvocation shellInvocation(Shell shell, const QString& command) {
  ShellInvocation invocation;
  if (shell == Shell::Cmd) {
    // The outer quotes keep cmd from stripping the first and last quote of the command.
    invocation.program = QStringLiteral("cmd.exe");
    invocation.nativeArgs = QStringLiteral("/c \"") + command + QStringLiteral("\"");
    return invocation;
  }
  invocation.program =
      shell == Shell::Pwsh ? QStringLiteral("pwsh.exe") : QStringLiteral("powershell.exe");
  const QByteArray utf16(reinterpret_cast<const char*>(command.utf16()),
                         static_cast<qsizetype>(command.size() * 2));
  invocation.args = {QStringLiteral("-NoLogo"),
                     QStringLiteral("-NoProfile"),
                     QStringLiteral("-NonInteractive"),
                     QStringLiteral("-WindowStyle"),
                     QStringLiteral("Hidden"),
                     QStringLiteral("-EncodedCommand"),
                     QString::fromLatin1(utf16.toBase64())};
  return invocation;
}

QString commandForForeground(const Binding& binding, const QStringList& blacklist,
                             const QString& foregroundProcess, QString* reason) {
  const auto skip = [reason](const QString& why) {
    if (reason) *reason = why;
    return QString();
  };
  if (!foregroundProcess.isEmpty()) {
    for (const QString& listed : blacklist) {
      if (processNamesMatch(listed, foregroundProcess)) {
        return skip(QStringLiteral("%1 is on the blacklist").arg(foregroundProcess));
      }
    }
  }
  if (binding.rules.empty()) return binding.command;
  const AppRule* fallback = nullptr;
  for (const AppRule& rule : binding.rules) {
    if (rule.isDefault) {
      fallback = &rule;
    } else if (!foregroundProcess.isEmpty() && processNamesMatch(rule.process, foregroundProcess)) {
      if (rule.ignore) return skip(QStringLiteral("%1 is set to Ignore").arg(rule.process));
      return rule.command;
    }
  }
  if (fallback == nullptr) return skip(QStringLiteral("no rule for %1").arg(foregroundProcess));
  if (fallback->ignore) return skip(QStringLiteral("Default is set to Ignore"));
  return fallback->command;
}

QProcessEnvironment withPathPrefix(const QProcessEnvironment& environment, const QString& folder) {
  QProcessEnvironment result = environment;
  if (folder.isEmpty()) return result;
  const QString native = QDir::toNativeSeparators(folder);
  const QString path = environment.value(QStringLiteral("PATH"));
  if (path.isEmpty()) {
    result.insert(QStringLiteral("PATH"), native);
  } else if (!path.startsWith(native + QLatin1Char(';'), Qt::CaseInsensitive) &&
             path.compare(native, Qt::CaseInsensitive) != 0) {
    result.insert(QStringLiteral("PATH"), native + QLatin1Char(';') + path);
  }
  return result;
}

}  // namespace waku::keymap

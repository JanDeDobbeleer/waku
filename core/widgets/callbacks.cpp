#include "core/widgets/callbacks.h"

#include <windows.h>

#include <QDebug>
#include <QProcess>
#include <QRegularExpression>
#include <iterator>

namespace yasb::widgets {

ParsedCallback parseCallback(const QString& callbackStr) {
  ParsedCallback out;
  if (!callbackStr.contains(QLatin1Char(' '))) {
    out.name = callbackStr;
    return out;
  }
  static const QRegularExpression re(QStringLiteral("\".+?\"|[^ ]+"));
  QStringList tokens;
  auto it = re.globalMatch(callbackStr);
  while (it.hasNext()) {
    QString t = it.next().captured(0);
    // Python's str.strip('"') removes every leading and trailing quote, not just a pair.
    qsizetype b = 0;
    qsizetype e = t.size();
    while (b < e && t[b] == QLatin1Char('"')) ++b;
    while (e > b && t[e - 1] == QLatin1Char('"')) --e;
    tokens << t.mid(b, e - b);
  }
  if (tokens.isEmpty()) return out;
  out.name = tokens.takeFirst();
  out.args = tokens;
  return out;
}

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

ProcessLauncher& processLauncher() {
  static ProcessLauncher launcher = [](const QString& commandLine) {
    QProcess proc;
    proc.setProgram(QStringLiteral("cmd.exe"));
    proc.setNativeArguments(QStringLiteral("/c ") + commandLine);
    return proc.startDetached();
  };
  return launcher;
}

SystemFunctionMap& systemFunctions() {
  static SystemFunctionMap map;
  return map;
}

namespace {
void sendKeys(std::initializer_list<WORD> keys) {
  for (WORD k : keys) keybd_event(static_cast<BYTE>(k), 0, 0, 0);
  for (auto it = std::rbegin(keys); it != std::rend(keys); ++it) {
    keybd_event(static_cast<BYTE>(*it), 0, KEYEVENTF_KEYUP, 0);
  }
}
}  // namespace

void registerDefaultSystemFunctions() {
  auto& m = systemFunctions();
  m[QStringLiteral("notification_center")] = [] { sendKeys({VK_LWIN, 'N'}); };
  m[QStringLiteral("quick_settings")] = [] { sendKeys({VK_LWIN, 'A'}); };
  m[QStringLiteral("search")] = [] { sendKeys({VK_LWIN, 'S'}); };
  m[QStringLiteral("widget")] = [] { sendKeys({VK_LWIN, 'W'}); };
  m[QStringLiteral("start_menu")] = [] { sendKeys({VK_LWIN}); };
}

CallbackRegistry::CallbackRegistry() {
  add(QStringLiteral("default"), [](const QStringList&) {}, 0);
  add(QStringLiteral("do_nothing"), [](const QStringList&) {}, 0);
  add(
      QStringLiteral("exec"),
      [](const QStringList& args) {
        if (args.isEmpty()) return;
        const auto& fns = systemFunctions();
        const auto found = fns.find(args.first());
        if (found != fns.end()) {
          found->second();
          return;
        }
        processLauncher()(quoteCmdLine(args));
      },
      -1);
}

void CallbackRegistry::add(const QString& name, CallbackFn fn, int arity) {
  callbacks_[name] = Entry{std::move(fn), arity};
}

bool CallbackRegistry::run(const QString& callbackStr) {
  const ParsedCallback parsed = parseCallback(callbackStr);
  const auto it = callbacks_.find(parsed.name);
  if (it == callbacks_.end()) {
    if (warned_.insert(parsed.name).second) {
      qWarning().noquote() << "Unknown callback" << parsed.name;
    }
    return false;
  }
  const Entry& entry = it->second;
  const bool arityOk = entry.arity < 0 || parsed.args.size() == entry.arity;
  if (!arityOk) {
    if (warned_.insert(parsed.name + QLatin1String("#arity")).second) {
      qWarning().noquote() << "Callback" << parsed.name << "called with" << parsed.args.size()
                           << "args";
    }
    return false;
  }
  try {
    entry.fn(parsed.args);
  } catch (const std::exception& e) {
    qWarning().noquote() << "Failed to execute callback" << parsed.name << ":" << e.what();
    return false;
  }
  return true;
}

}  // namespace yasb::widgets

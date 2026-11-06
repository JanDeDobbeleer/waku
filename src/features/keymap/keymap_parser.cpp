#include "features/keymap/keymap_parser.h"

#include <QRegularExpression>
#include <map>

#include "features/keymap/key_names.h"
#include "features/keymap/keymap_action.h"

namespace waku::keymap {

QString issueText(const KeymapIssue& issue) {
  return QStringLiteral("line %1: %2").arg(issue.line).arg(issue.message);
}

QString stripComment(const QString& text) {
  QChar quote;
  for (qsizetype i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (!quote.isNull()) {
      if (c == quote) quote = QChar();
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      quote = c;
    } else if (c == QLatin1Char('#') && (i == 0 || text.at(i - 1).isSpace())) {
      return text.left(i).trimmed();
    }
  }
  return text.trimmed();
}

namespace {

struct Builder {
  KeymapParseResult result;
  std::map<kernel::HotkeyCombo, int> firstLine;  // combo -> line of the binding that owns it

  void issue(int line, const QString& message) { result.issues.push_back({line, message}); }

  // Checks a command: a `waku:` action must be known and well formed.
  bool commandOk(int line, const QString& command) {
    const ActionParse parsed = parseAction(command);
    if (!parsed.action) {
      issue(line, parsed.error);
      return false;
    }
    return true;
  }

  // Parses the keys of a binding; false (and an issue) when they are bad or taken.
  bool takeKeys(int line, const QString& keysText, Binding* binding) {
    const ComboParse parsed = parseCombo(keysText);
    if (!parsed.combo) {
      issue(line, parsed.error);
      return false;
    }
    const auto owner = firstLine.find(*parsed.combo);
    if (owner != firstLine.end()) {
      issue(line, QStringLiteral("'%1' is already bound on line %2; this line is ignored")
                      .arg(keysText.simplified())
                      .arg(owner->second));
      return false;
    }
    binding->combo = *parsed.combo;
    binding->keys = keysText.simplified();
    binding->line = line;
    return true;
  }

  void add(Binding binding) {
    firstLine.emplace(binding.combo, binding.line);
    result.keymap.bindings.push_back(std::move(binding));
  }
};

// The entries of `.blacklist [ "a.exe", "b.exe" ]`: quoted strings, or bare words split on commas
// and spaces.
QStringList listEntries(const QString& body) {
  QStringList entries;
  static const QRegularExpression quoted(QStringLiteral("\"([^\"]*)\""));
  auto matches = quoted.globalMatch(body);
  bool anyQuoted = false;
  while (matches.hasNext()) {
    anyQuoted = true;
    const QString entry = matches.next().captured(1).trimmed();
    if (!entry.isEmpty()) entries << entry;
  }
  if (anyQuoted) return entries;
  for (const QString& word :
       body.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
    entries << word;
  }
  return entries;
}

}  // namespace

KeymapParseResult parseKeymap(const QString& text) {
  Builder builder;
  Keymap& keymap = builder.result.keymap;
  const QStringList lines = text.split(QLatin1Char('\n'));

  bool inBlock = false;
  Binding block;
  bool blockValid = false;  // the keys of the block were accepted
  bool inBlacklist = false;
  int blacklistLine = 0;
  QString blacklistText;

  for (int index = 0; index < lines.size(); ++index) {
    const int lineNo = index + 1;
    QString line = lines.at(index);
    if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
    if (lineNo == 1 && line.startsWith(QChar(0xFEFF))) line.remove(0, 1);  // BOM
    line = stripComment(line);

    if (inBlacklist) {
      blacklistText += QLatin1Char(' ') + line;
      if (line.contains(QLatin1Char(']'))) {
        inBlacklist = false;
        keymap.blacklist +=
            listEntries(blacklistText.left(blacklistText.indexOf(QLatin1Char(']'))));
      }
      continue;
    }
    if (line.isEmpty()) continue;

    if (inBlock) {
      if (line == QLatin1String("]")) {
        inBlock = false;
        if (blockValid) {
          if (block.rules.empty()) {
            builder.issue(block.line, QStringLiteral("the block has no rules; ignored"));
          } else {
            builder.add(block);
          }
        }
        continue;
      }
      const qsizetype colon = line.indexOf(QLatin1Char(':'));
      const QString name = colon < 0 ? QString() : line.left(colon).trimmed();
      const QString command = colon < 0 ? QString() : line.mid(colon + 1).trimmed();
      if (name.isEmpty() || command.isEmpty()) {
        builder.issue(lineNo, QStringLiteral("expected 'ProcessName : command' in a block"));
        continue;
      }
      AppRule rule;
      rule.line = lineNo;
      rule.isDefault = name.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0;
      rule.process = rule.isDefault ? QString() : name;
      rule.ignore = command.compare(QLatin1String("ignore"), Qt::CaseInsensitive) == 0;
      if (!rule.ignore) {
        if (!builder.commandOk(lineNo, command)) continue;
        rule.command = command;
      }
      block.rules.push_back(std::move(rule));
      continue;
    }

    if (line.startsWith(QLatin1Char('.'))) {
      qsizetype end = 1;
      while (end < line.size() && !line.at(end).isSpace() && line.at(end) != QLatin1Char('[')) {
        ++end;
      }
      const QString name = line.mid(1, end - 1).toLower();
      const QString arg = line.mid(end).trimmed();
      if (name == QLatin1String("shell")) {
        const QString shell = arg.toLower();
        if (shell == QLatin1String("cmd")) {
          keymap.shell = Shell::Cmd;
        } else if (shell == QLatin1String("pwsh")) {
          keymap.shell = Shell::Pwsh;
        } else if (shell == QLatin1String("powershell")) {
          keymap.shell = Shell::Powershell;
        } else {
          builder.issue(lineNo,
                        QStringLiteral("unknown shell '%1' (cmd, pwsh or powershell)").arg(arg));
        }
      } else if (name == QLatin1String("pause")) {
        const ComboParse parsed = parseCombo(arg);
        if (!parsed.combo) {
          builder.issue(lineNo, QStringLiteral(".pause: %1").arg(parsed.error));
        } else {
          keymap.pause = parsed.combo;
          keymap.pauseKeys = arg.simplified();
          keymap.pauseLine = lineNo;
        }
      } else if (name == QLatin1String("pause_hook")) {
        if (arg.isEmpty() || builder.commandOk(lineNo, arg)) keymap.pauseHook = arg;
      } else if (name == QLatin1String("blacklist")) {
        if (!arg.startsWith(QLatin1Char('['))) {
          builder.issue(lineNo, QStringLiteral(".blacklist needs a list: [ \"app.exe\", ... ]"));
        } else if (arg.contains(QLatin1Char(']'))) {
          keymap.blacklist += listEntries(arg.mid(1, arg.indexOf(QLatin1Char(']')) - 1));
        } else {
          inBlacklist = true;
          blacklistLine = lineNo;
          blacklistText = arg.mid(1);
        }
      } else {
        builder.issue(lineNo, QStringLiteral("unknown directive '.%1'").arg(name));
      }
      continue;
    }

    if (line.endsWith(QLatin1Char('[')) && !line.contains(QLatin1Char(':'))) {
      block = Binding();
      block.line = lineNo;
      blockValid = builder.takeKeys(lineNo, line.chopped(1), &block);
      inBlock = true;  // the lines up to `]` are consumed even if the keys were bad
      continue;
    }

    const qsizetype colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0) {
      builder.issue(lineNo, QStringLiteral("expected 'keys : command'"));
      continue;
    }
    const QString keysText = line.left(colon);
    const QString command = line.mid(colon + 1).trimmed();
    Binding binding;
    if (!builder.takeKeys(lineNo, keysText, &binding)) continue;
    if (command.isEmpty()) {
      builder.issue(lineNo, QStringLiteral("empty command for '%1'").arg(binding.keys));
      continue;
    }
    if (!builder.commandOk(lineNo, command)) continue;
    binding.command = command;
    builder.add(std::move(binding));
  }

  if (inBlock) {
    builder.issue(block.line, QStringLiteral("the block is not closed with ']'; ignored"));
  }
  if (inBlacklist) {
    builder.issue(blacklistLine, QStringLiteral(".blacklist is not closed with ']'"));
  }
  if (keymap.pause) {
    const auto owner = builder.firstLine.find(*keymap.pause);
    if (owner != builder.firstLine.end()) {
      builder.issue(
          keymap.pauseLine,
          QStringLiteral(".pause uses the keys of line %1; .pause is ignored").arg(owner->second));
      keymap.pause.reset();
      keymap.pauseKeys.clear();
    }
  }
  return std::move(builder.result);
}

}  // namespace waku::keymap

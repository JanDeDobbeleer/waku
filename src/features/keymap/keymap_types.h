#pragma once

#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "kernel/qt/hotkeys.h"

namespace waku::keymap {

// The shell that runs a plain command (`.shell` in a whkd file).
enum class Shell { Cmd, Pwsh, Powershell };

// One line of a per-app block: `Firefox : command`, `Default : command` or `Chrome : Ignore`.
struct AppRule {
  QString process;  // as written; empty for Default
  QString command;  // empty for Ignore
  bool isDefault = false;
  bool ignore = false;
  int line = 0;
};

// One binding. A plain binding has `command`; a block has `rules`.
struct Binding {
  kernel::HotkeyCombo combo;
  QString keys;  // as written, for messages
  int line = 0;
  QString command;
  std::vector<AppRule> rules;
};

struct Keymap {
  Shell shell = Shell::Powershell;
  std::vector<Binding> bindings;
  QStringList blacklist;  // executable names; bindings are ignored while one is in front
  std::optional<kernel::HotkeyCombo> pause;
  QString pauseKeys;
  int pauseLine = 0;
  QString pauseHook;  // a shell command run when the pause toggles
};

struct KeymapIssue {
  int line = 0;
  QString message;
};

struct KeymapParseResult {
  Keymap keymap;
  std::vector<KeymapIssue> issues;  // the lines that were skipped or only partly understood
};

// "line 12: unknown key 'foo'".
QString issueText(const KeymapIssue& issue);

}  // namespace waku::keymap

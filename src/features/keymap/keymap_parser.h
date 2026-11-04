#pragma once

#include <QString>

#include "features/keymap/keymap_types.h"

namespace waku::keymap {

// Parses a whkd binding file (the syntax is in design.md). Pure: no file, no Windows call. A bad
// line is reported in `issues` and skipped; the other lines load.
//
//   .shell cmd|pwsh|powershell
//   .pause <keys>                 toggles every other binding off and on
//   .pause_hook <command>         runs when the pause toggles
//   .blacklist [ "app.exe", ... ] bindings do nothing while one of these is in front (waku)
//   <keys> : <command>            `alt + shift + oem_4 : komorebic cycle-focus previous`
//   <keys> [                      a per-app block, closed by a line `]`:
//     Firefox : <command>         ProcessName : command
//     Default : <command>         every app not listed
//     Chrome  : Ignore            this app: nothing
//   ]
//   # comment                     a whole line, or after a command when `#` follows a space
//
// Line endings may be LF or CRLF.
KeymapParseResult parseKeymap(const QString& text);

// `text` without a trailing `# comment`: a `#` starts one when it is first on the line or follows
// whitespace, outside quotes.
QString stripComment(const QString& text);

}  // namespace waku::keymap

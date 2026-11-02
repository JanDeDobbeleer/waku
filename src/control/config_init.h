#pragma once

#include <QString>

namespace waku::control {

// `wakuc config init`: writes a starter shell.qml into a config home. Client side only: it needs no
// running bar and no control pipe.

// The starter text for a template (the built-in bar's DefaultShell.qml): the template's leading
// `//` comment block is replaced by a header that says what the file is and how to edit it. The
// rest is kept as it is.
QString starterShellText(const QString& templateText);

struct ConfigInitResult {
  enum Status { Written, AlreadyExists, Failed };
  Status status = Failed;
  QString path;     // the shell.qml, in native separators
  QString message;  // one line for the user; empty when Written
};

// Creates `configHome` (and its parents) and writes `<configHome>/shell.qml` atomically. An
// existing shell.qml is kept (AlreadyExists) unless `force`. No other file is touched.
ConfigInitResult initConfigHome(const QString& configHome, const QString& shellText, bool force);

}  // namespace waku::control

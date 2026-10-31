#pragma once

#include <QString>

namespace wabar::kernel {

// `text` with every %VARIABLE% replaced by its value (ExpandEnvironmentStringsW, documented).
// Unknown variables stay as written; `text` itself when it has no '%' or the call fails.
QString expandEnvironment(const QString& text);

}  // namespace wabar::kernel

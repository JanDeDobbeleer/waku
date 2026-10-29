#pragma once

#include <QString>
#include <QStringList>

namespace wabar::workspaces {

// Windows list2cmdline quoting: joins argv into one command line, as subprocess.list2cmdline
// builds it.
QString quoteCmdLine(const QStringList& argv);

}  // namespace wabar::workspaces

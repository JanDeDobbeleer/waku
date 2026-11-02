#pragma once

#include <QString>
#include <QStringList>

class QProcess;

namespace waku::kernel {

// utils/shell_utils.shell_open: ShellExecuteW ("open") on a detached thread so a slow launch never
// blocks the GUI thread. Returns at once; failures are logged.
void shellOpen(const QString& file, const QString& parameters = QString());

// Makes `process` start without a console window (CREATE_NO_WINDOW). Call before start().
void hideConsole(QProcess& process);

// Fire-and-forget `program args` without a console window and with stdout and stderr discarded.
// `nativeArgs`, when not empty, is passed to CreateProcess verbatim (QProcess::setNativeArguments).
bool startDetachedHidden(const QString& program, const QStringList& args,
                         const QString& nativeArgs = QString());

// Fire-and-forget `cmd.exe /c "<commandLine>"` without a console window (Python's shell=True
// path). The outer quotes keep cmd from stripping the first and last quote of a command that
// starts with one. Failures are logged.
bool launchHiddenShell(const QString& commandLine);

}  // namespace waku::kernel

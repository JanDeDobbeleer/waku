#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class QProcess;

namespace waku::kernel {

// utils/shell_utils.shell_open: ShellExecuteW ("open") on a detached thread so a slow launch never
// blocks the GUI thread. Returns at once; failures are logged.
void shellOpen(const QString& file, const QString& parameters = QString());

// ShellExecuteEx with the "runas" verb (UAC prompt) on a detached thread. Returns at once: the call
// blocks until the prompt is answered and pumps window messages meanwhile, which on the GUI thread
// lets the caller's own windows close under it. Failures (including a declined prompt) are logged.
// `parameters` is a Windows command line tail. waku extension for apps and `execDetached`.
void shellRunElevated(const QString& file, const QString& parameters = QString(),
                      const QString& workingDirectory = QString());

// Makes `process` start without a console window (CREATE_NO_WINDOW). Call before start().
void hideConsole(QProcess& process);

// Fire-and-forget `program args` without a console window and with stdout and stderr discarded.
// `nativeArgs`, when not empty, is passed to CreateProcess verbatim (QProcess::setNativeArguments).
// `environment`, when not null, is the environment of the new process (default: ours).
bool startDetachedHidden(const QString& program, const QStringList& args,
                         const QString& nativeArgs = QString(),
                         const QProcessEnvironment* environment = nullptr);

// Fire-and-forget `cmd.exe /c "<commandLine>"` without a console window (Python's shell=True
// path). The outer quotes keep cmd from stripping the first and last quote of a command that
// starts with one. Failures are logged.
bool launchHiddenShell(const QString& commandLine);

}  // namespace waku::kernel

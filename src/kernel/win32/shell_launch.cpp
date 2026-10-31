#include "kernel/win32/shell_launch.h"

#include <windows.h>
#include <shellapi.h>

#include <QDebug>
#include <QProcess>
#include <string>
#include <thread>

#include "kernel/win32/com_scope.h"

namespace wabar::kernel {

void shellOpen(const QString& file, const QString& parameters) {
  const std::wstring target = file.toStdWString();
  const std::wstring args = parameters.toStdWString();
  std::thread([target, args] {
    const ComScope com(ComApartment::SingleThreaded, true);
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", target.c_str(), args.empty() ? nullptr : args.c_str(),
                      nullptr, SW_SHOWNORMAL));
    if (result <= 32)
      qWarning().noquote() << "ShellExecute failed for" << QString::fromStdWString(target);
  }).detach();
}

void hideConsole(QProcess& process) {
  process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
}

bool startDetachedHidden(const QString& program, const QStringList& args,
                         const QString& nativeArgs) {
  QProcess process;
  hideConsole(process);
  process.setProgram(program);
  process.setArguments(args);
  if (!nativeArgs.isEmpty()) process.setNativeArguments(nativeArgs);
  process.setStandardOutputFile(QProcess::nullDevice());
  process.setStandardErrorFile(QProcess::nullDevice());
  return process.startDetached();
}

bool launchHiddenShell(const QString& commandLine) {
  const bool ok = startDetachedHidden(QStringLiteral("cmd.exe"), {},
                                      QStringLiteral("/c \"") + commandLine + QStringLiteral("\""));
  if (!ok) qWarning().noquote() << "Failed to run" << commandLine;
  return ok;
}

}  // namespace wabar::kernel

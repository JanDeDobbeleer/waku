#include "kernel/win32/shell_launch.h"

#include <windows.h>
#include <combaseapi.h>
#include <shellapi.h>

#include <QDebug>
#include <QProcess>
#include <string>
#include <thread>

namespace wabar::kernel {

void shellOpen(const QString& file, const QString& parameters) {
  const std::wstring target = file.toStdWString();
  const std::wstring args = parameters.toStdWString();
  std::thread([target, args] {
    const bool initialised =
        SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", target.c_str(), args.empty() ? nullptr : args.c_str(),
                      nullptr, SW_SHOWNORMAL));
    if (result <= 32)
      qWarning().noquote() << "ShellExecute failed for" << QString::fromStdWString(target);
    if (initialised) CoUninitialize();
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

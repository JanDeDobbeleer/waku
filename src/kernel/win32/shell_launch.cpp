#include "kernel/win32/shell_launch.h"

#include <windows.h>
#include <shellapi.h>

#include <QDebug>
#include <QDir>
#include <QProcess>
#include <string>
#include <thread>

#include "kernel/win32/com_scope.h"

namespace waku::kernel {

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

void shellRunElevated(const QString& file, const QString& parameters,
                      const QString& workingDirectory) {
  const std::wstring target = QDir::toNativeSeparators(file).toStdWString();
  const std::wstring args = parameters.toStdWString();
  const std::wstring directory = QDir::toNativeSeparators(workingDirectory).toStdWString();
  std::thread([target, args, directory] {
    const ComScope com(ComApartment::SingleThreaded, true);
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = target.c_str();
    info.lpParameters = args.empty() ? nullptr : args.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
      qWarning().noquote() << "Elevated start failed or declined for"
                           << QString::fromStdWString(target) << "error" << GetLastError();
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

}  // namespace waku::kernel

#include <windows.h>
#include <shellapi.h>

#include <QDebug>
#include <QDir>

#include "features/quickshell_io/command_spec.h"

namespace waku::quickshell_io {

// ShellExecuteEx "runas" is documented. It raises the UAC prompt; the child is not a child of ours
// and cannot be piped. Runs on the GUI thread: the call returns once the prompt is answered.
bool startElevated(const CommandSpec& spec) {
  if (spec.command.isEmpty()) return false;
  if (!spec.environment.isEmpty() || spec.clearEnvironment) {
    qWarning() << "elevated launch ignores the environment of" << spec.command.first();
  }
  const std::wstring file = QDir::toNativeSeparators(spec.command.first()).toStdWString();
  const std::wstring parameters = windowsArguments(spec.command.mid(1)).toStdWString();
  const std::wstring directory = QDir::toNativeSeparators(spec.workingDirectory).toStdWString();
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
  info.lpVerb = L"runas";
  info.lpFile = file.c_str();
  info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
  info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

}  // namespace waku::quickshell_io

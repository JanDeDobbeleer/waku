#include "platform/shell_open.h"

#include <windows.h>
#include <combaseapi.h>
#include <shellapi.h>

#include <QDebug>
#include <string>
#include <thread>

namespace yasb::platform {

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

}  // namespace yasb::platform

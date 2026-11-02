#include <windows.h>
#include <shellapi.h>

#include <QDir>
#include <QFileInfo>

#include "features/apps/apps_platform.h"
#include "kernel/win32/app_activation.h"

namespace waku::apps {
namespace {

bool shellExecute(const QString& file, const QString& parameters, const wchar_t* verb,
                  const QString& workingDirectory = QString()) {
  const std::wstring nativeFile = QDir::toNativeSeparators(file).toStdWString();
  const std::wstring params = parameters.toStdWString();
  const std::wstring directory = QDir::toNativeSeparators(workingDirectory).toStdWString();
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
  info.lpVerb = verb;
  info.lpFile = nativeFile.c_str();
  info.lpParameters = params.empty() ? nullptr : params.c_str();
  info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

bool launch(const AppRecord& app, LaunchMode mode) {
  switch (mode) {
    case LaunchMode::Normal:
      if (app.kind == AppKind::Store) {
        if (app.aumid.isEmpty()) return false;
        // Raise the window of a running app; otherwise start it.
        if (waku::kernel::activateAppByAumid(app.aumid)) return true;
        return shellExecute(QStringLiteral("shell:AppsFolder\\") + app.aumid, QString(), nullptr);
      }
      return !app.path.isEmpty() && shellExecute(app.path, QString(), nullptr);
    case LaunchMode::Admin:
      // A packaged app starts elevated only through its execution alias.
      if (app.kind == AppKind::Store)
        return !app.elevationAlias.isEmpty() &&
               shellExecute(app.elevationAlias, QString(), L"runas");
      if (app.path.isEmpty()) return false;
      return shellExecute(app.path, QString(), L"runas");
    case LaunchMode::OpenLocation: {
      if (app.kind == AppKind::Store) return false;
      const QString file =
          !app.target.isEmpty() && QFileInfo::exists(app.target) ? app.target : app.path;
      if (file.isEmpty()) return false;
      return shellExecute(
          QStringLiteral("explorer.exe"),
          QStringLiteral("/select,\"") + QDir::toNativeSeparators(file) + QStringLiteral("\""),
          nullptr);
    }
  }
  return false;
}

}  // namespace

AppLauncher makeWin32Launcher() { return &launch; }

}  // namespace waku::apps

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl_core.h>

#include <QDir>
#include <QFileInfo>
#include <vector>

#include "features/apps/apps_platform.h"
#include "kernel/win32/com_scope.h"

namespace wabar::apps {
namespace {

QString expandEnvironment(const QString& text) {
  if (!text.contains(QLatin1Char('%'))) return text;
  const auto source = reinterpret_cast<PCWSTR>(text.utf16());
  const DWORD needed = ExpandEnvironmentStringsW(source, nullptr, 0);
  if (needed == 0) return text;
  std::vector<wchar_t> buffer(needed);
  if (ExpandEnvironmentStringsW(source, buffer.data(), needed) == 0) return text;
  return QString::fromWCharArray(buffer.data());
}

ShortcutInfo resolveShortcut(const QString& lnkPath) {
  ShortcutInfo info;
  // Hidden shortcuts are not part of the Start Menu the user sees.
  const DWORD attributes =
      GetFileAttributesW(reinterpret_cast<PCWSTR>(QDir::toNativeSeparators(lnkPath).utf16()));
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_HIDDEN)) return info;
  const kernel::ComScope com(kernel::ComApartment::MultiThreaded);
  IShellLinkW* link = nullptr;
  if (FAILED(
          CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
      !link) {
    return info;
  }
  IPersistFile* file = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) && file) {
    const QString native = QDir::toNativeSeparators(lnkPath);
    if (SUCCEEDED(file->Load(reinterpret_cast<PCWSTR>(native.utf16()), STGM_READ))) {
      // No Resolve(): it searches for a moved target and can take seconds. The stored path is
      // what the shortcut says; ShellExecute resolves it at launch.
      wchar_t buffer[MAX_PATH * 4] = {};
      if (SUCCEEDED(
              link->GetPath(buffer, static_cast<int>(std::size(buffer)), nullptr, SLGP_RAWPATH))) {
        info.target = expandEnvironment(QString::fromWCharArray(buffer));
      }
      buffer[0] = 0;
      if (SUCCEEDED(link->GetArguments(buffer, static_cast<int>(std::size(buffer)))))
        info.arguments = expandEnvironment(QString::fromWCharArray(buffer));
      buffer[0] = 0;
      if (SUCCEEDED(link->GetWorkingDirectory(buffer, static_cast<int>(std::size(buffer)))))
        info.workingDirectory = expandEnvironment(QString::fromWCharArray(buffer));
      buffer[0] = 0;
      if (SUCCEEDED(link->GetDescription(buffer, static_cast<int>(std::size(buffer)))))
        info.description = QString::fromWCharArray(buffer);
      info.ok = true;
    }
    file->Release();
  }
  link->Release();
  return info;
}

QString knownFolder(const KNOWNFOLDERID& id) {
  PWSTR path = nullptr;
  QString result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)) && path)
    result = QString::fromWCharArray(path);
  CoTaskMemFree(path);
  return result;
}

}  // namespace

ShortcutResolver makeWin32ShortcutResolver() { return &resolveShortcut; }

QStringList startMenuFolders() {
  QStringList folders;
  for (const QString& folder :
       {knownFolder(FOLDERID_Programs), knownFolder(FOLDERID_CommonPrograms)}) {
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) folders.append(folder);
  }
  return folders;
}

}  // namespace wabar::apps

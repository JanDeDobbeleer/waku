// Media source app helpers (ports of services/media/source_apps.py, utils/win32/aumid.py and
// aumid_process.py). Name resolution runs on the media worker thread; activation runs on the GUI
// thread from a click.
// clang-format off
// The Windows SDK headers below depend on each other's order (propkey.h needs propsys.h).
#include <windows.h>
#include <appmodel.h>
#include <propsys.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
// clang-format on

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/base.h>

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <optional>
#include <utility>
#include <vector>

#include "platform/media_session.h"

namespace yasb::platform {
namespace {

struct ProcessEntry {
  DWORD pid = 0;
  DWORD parentPid = 0;
  QString exe;
};

std::vector<ProcessEntry> snapshotProcesses() {
  std::vector<ProcessEntry> result;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return result;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      result.push_back({entry.th32ProcessID, entry.th32ParentProcessID,
                        QString::fromWCharArray(entry.szExeFile)});
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return result;
}

QString baseName(const QString& path) {
  const qsizetype slash = std::max(path.lastIndexOf(u'\\'), path.lastIndexOf(u'/'));
  return slash >= 0 ? path.mid(slash + 1) : path;
}

QString stem(const QString& file) {
  const qsizetype dot = file.lastIndexOf(u'.');
  return dot > 0 ? file.left(dot) : file;
}

QString processAumid(DWORD pid) {
  if (!pid) return {};
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  wchar_t buffer[APPLICATION_USER_MODEL_ID_MAX_LENGTH]{};
  UINT32 length = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
  QString result;
  if (GetApplicationUserModelId(process, &length, buffer) == ERROR_SUCCESS)
    result = QString::fromWCharArray(buffer);
  CloseHandle(process);
  return result;
}

DWORD windowPid(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  return pid;
}

QString windowAumid(HWND hwnd) {
  IPropertyStore* store = nullptr;
  if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) && store) {
    PROPVARIANT value;
    PropVariantInit(&value);
    QString aumid;
    bool found = false;
    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal) {
      aumid = QString::fromWCharArray(value.pwszVal);
      found = true;
    }
    PropVariantClear(&value);
    store->Release();
    if (found) return aumid;
  }
  return processAumid(windowPid(hwnd));
}

std::vector<HWND> topLevelWindows() {
  std::vector<HWND> windows;
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&windows));
  return windows;
}

QString windowClass(HWND hwnd) {
  wchar_t buffer[256]{};
  const int length = GetClassNameW(hwnd, buffer, 256);
  return QString::fromWCharArray(buffer, std::max(0, length));
}

DWORD pidForWindowAumid(const QString& aumid) {
  if (aumid.isEmpty()) return 0;
  for (HWND hwnd : topLevelWindows()) {
    if (!IsWindowVisible(hwnd)) continue;
    if (windowAumid(hwnd).compare(aumid, Qt::CaseInsensitive) == 0) {
      if (const DWORD pid = windowPid(hwnd)) return pid;
    }
  }
  return 0;
}

QString processPath(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  wchar_t buffer[MAX_PATH * 4]{};
  DWORD size = MAX_PATH * 4;
  QString path;
  if (QueryFullProcessImageNameW(process, 0, buffer, &size))
    path = QString::fromWCharArray(buffer, size);
  CloseHandle(process);
  return path;
}

QString versionString(const QString& exePath, const wchar_t* key) {
  const std::wstring path = exePath.toStdWString();
  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
  if (!size) return {};
  std::vector<unsigned char> data(size);
  if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
  struct Translation {
    WORD language;
    WORD codePage;
  };
  Translation* translations = nullptr;
  UINT length = 0;
  std::vector<Translation> candidates;
  if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                     reinterpret_cast<void**>(&translations), &length) &&
      translations) {
    candidates.assign(translations, translations + length / sizeof(Translation));
  }
  candidates.push_back({0x0409, 0x04B0});
  candidates.push_back({0x0409, 0x04E4});
  for (const Translation& t : candidates) {
    wchar_t query[128];
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", t.language, t.codePage, key);
    wchar_t* value = nullptr;
    UINT valueLength = 0;
    if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &valueLength) &&
        value && valueLength > 0) {
      const QString text = QString::fromWCharArray(value).trimmed();
      if (!text.isEmpty()) return text;
    }
  }
  return {};
}

// utils.get_app_name_from_pid (packaged processes resolve through AppInfo instead).
QString appNameFromPid(DWORD pid) {
  const QString path = processPath(pid);
  if (path.isEmpty()) return {};
  QString name = versionString(path, L"FileDescription");
  if (name.isEmpty()) name = versionString(path, L"ProductName");
  return name;
}

QString appInfoName(const QString& aumid) {
  try {
    const auto info =
        winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(aumid.toStdWString());
    if (!info) return {};
    return QString::fromStdWString(std::wstring(info.DisplayInfo().DisplayName())).trimmed();
  } catch (...) {
    return {};
  }
}

// aumid_process.resolve_shell_app: shell:AppsFolder item name and link target exe.
std::pair<QString, QString> resolveShellApp(const QString& aumid) {
  static QMutex mutex;
  static QHash<QString, std::pair<QString, QString>> cache;
  {
    QMutexLocker lock(&mutex);
    if (auto it = cache.constFind(aumid); it != cache.constEnd()) return *it;
  }
  std::pair<QString, QString> result;
  const std::wstring path = (QStringLiteral("shell:AppsFolder\\") + aumid).toStdWString();
  IShellItem2* item = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) && item) {
    PWSTR name = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
      result.first = QString::fromWCharArray(name).trimmed();
      CoTaskMemFree(name);
    }
    PWSTR target = nullptr;
    if (SUCCEEDED(item->GetString(PKEY_Link_TargetParsingPath, &target)) && target) {
      result.second = baseName(QString::fromWCharArray(target));
      CoTaskMemFree(target);
    }
    item->Release();
  }
  QMutexLocker lock(&mutex);
  cache.insert(aumid, result);
  return result;
}

// aumid_process.get_process_name_for_aumid.
QString processNameForAumid(const QString& aumid) {
  if (aumid.isEmpty()) return {};
  if (aumid.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) return baseName(aumid);
  for (const ProcessEntry& process : snapshotProcesses()) {
    if (processAumid(process.pid) == aumid) {
      if (process.exe.compare(QStringLiteral("pwahelper.exe"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("msedge.exe");
      return baseName(process.exe);
    }
  }
  if (const DWORD pid = pidForWindowAumid(aumid)) {
    const QString path = processPath(pid);
    if (!path.isEmpty()) return baseName(path);
    for (const ProcessEntry& process : snapshotProcesses()) {
      if (process.pid == pid && !process.exe.isEmpty()) return baseName(process.exe);
    }
  }
  return resolveShellApp(aumid).second;
}

QString nameFromProcess(const QString& aumid) {
  const QString process = processNameForAumid(aumid);
  if (process.isEmpty()) return {};
  for (const ProcessEntry& entry : snapshotProcesses()) {
    if (baseName(entry.exe).compare(process, Qt::CaseInsensitive) == 0) {
      const QString name = appNameFromPid(entry.pid);
      if (!name.isEmpty()) return name;
      break;
    }
  }
  return stem(process);
}

QString humanize(const QString& aumid) {
  if (aumid.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) return stem(baseName(aumid));
  if (aumid.contains(u'!')) return aumid.section(u'!', -1);
  QString last = aumid.contains(u'.') ? aumid.section(u'.', -1) : aumid;
  return last.replace(u'-', u' ').replace(u'_', u' ');
}

std::optional<HWND> findWindowByAumid(const QString& aumid, const QString& fallbackProcess) {
  std::vector<HWND> windows;
  for (HWND hwnd : topLevelWindows()) {
    if (IsWindowVisible(hwnd) && windowClass(hwnd) != QStringLiteral("Windows.UI.Core.CoreWindow"))
      windows.push_back(hwnd);
  }
  for (HWND hwnd : windows) {
    if (windowAumid(hwnd) == aumid) return hwnd;
  }
  if (fallbackProcess.isEmpty()) return std::nullopt;
  QHash<DWORD, QList<DWORD>> children;
  QList<DWORD> stack;
  for (const ProcessEntry& entry : snapshotProcesses()) {
    children[entry.parentPid].append(entry.pid);
    if (entry.exe.compare(fallbackProcess, Qt::CaseInsensitive) == 0) stack.append(entry.pid);
  }
  QSet<DWORD> pids;
  while (!stack.isEmpty()) {
    const DWORD pid = stack.takeLast();
    if (pids.contains(pid)) continue;
    pids.insert(pid);
    stack.append(children.value(pid));
  }
  for (HWND hwnd : windows) {
    if (pids.contains(windowPid(hwnd)) && GetWindowTextLengthW(hwnd) > 0) return hwnd;
  }
  return std::nullopt;
}

}  // namespace

QString resolveSourceAppName(const QString& aumid) {
  if (aumid.isEmpty()) return {};
  static QMutex mutex;
  static QHash<QString, QString> cache;
  {
    QMutexLocker lock(&mutex);
    if (auto it = cache.constFind(aumid); it != cache.constEnd()) return *it;
  }
  QString name = appInfoName(aumid);
  if (name.isEmpty() && !aumid.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
    name = resolveShellApp(aumid).first;
  if (name.isEmpty()) name = nameFromProcess(aumid);
  if (name.isEmpty()) {
    if (const DWORD pid = pidForWindowAumid(aumid)) name = appNameFromPid(pid);
  }
  if (name.isEmpty()) name = humanize(aumid);
  QMutexLocker lock(&mutex);
  cache.insert(aumid, name);
  return name;
}

QString aumidForPid(unsigned long pid) { return processAumid(static_cast<DWORD>(pid)); }

QString exeNameForAumid(const QString& aumid) { return processNameForAumid(aumid); }

QString sourceAppClassName(const QString& displayName) {
  return displayName.toLower().replace(u' ', u'-');
}

bool activateAppByAumid(const QString& aumid) {
  if (aumid.isEmpty()) return false;
  const auto hwnd = findWindowByAumid(aumid, processNameForAumid(aumid));
  if (!hwnd) return false;
  if (IsIconic(*hwnd)) PostMessageW(*hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
  SetWindowPos(*hwnd, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
  SetForegroundWindow(*hwnd);
  return true;
}

}  // namespace yasb::platform

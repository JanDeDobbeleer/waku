#include "features/taskbar/app_identity.h"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <ocidl.h>
#include <exdisp.h>
#include <appmodel.h>
#include <propsys.h>
#include <propkey.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propvarutil.h>
#include <shldisp.h>
#include <shlobj.h>
#include <winternl.h>
// clang-format on

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <vector>

#include "kernel/win32/com_scope.h"

namespace wabar::taskbar {
namespace {

HWND toHwnd(WindowHandle hwnd) { return reinterpret_cast<HWND>(hwnd); }

QString fromWide(const wchar_t* text) { return QString::fromWCharArray(text); }

std::optional<QString> processAumid(std::uint32_t pid) {
  if (pid == 0) return std::nullopt;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return std::nullopt;
  UINT32 length = 0;
  std::optional<QString> result;
  if (GetApplicationUserModelId(process, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER &&
      length > 0) {
    std::vector<wchar_t> buffer(length);
    if (GetApplicationUserModelId(process, &length, buffer.data()) == ERROR_SUCCESS)
      result = fromWide(buffer.data());
  }
  CloseHandle(process);
  return result;
}

// Python's fallback for windows without a LocationURL: Document.Folder.Self.Path, kept only when it
// is a CLSID path ("::{645FF040-...}" for the Recycle Bin), which is language independent.
std::optional<QString> documentFolderPath(IWebBrowser2* browser) {
  IDispatch* document = nullptr;
  if (FAILED(browser->get_Document(&document)) || !document) return std::nullopt;
  std::optional<QString> result;
  IShellFolderViewDual* view = nullptr;
  if (SUCCEEDED(document->QueryInterface(IID_PPV_ARGS(&view))) && view) {
    ::Folder* folder = nullptr;
    if (SUCCEEDED(view->get_Folder(&folder)) && folder) {
      // Self lives on Folder2.
      ::Folder2* folder2 = nullptr;
      if (SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&folder2))) && folder2) {
        ::FolderItem* self = nullptr;
        if (SUCCEEDED(folder2->get_Self(&self)) && self) {
          BSTR path = nullptr;
          if (SUCCEEDED(self->get_Path(&path)) && path) {
            const QString text = QString::fromWCharArray(path);
            if (text.contains(QLatin1String("::{"))) result = text;
            SysFreeString(path);
          }
          self->Release();
        }
        folder2->Release();
      }
      folder->Release();
    }
    view->Release();
  }
  document->Release();
  return result;
}

}  // namespace

QString normalizePath(const QString& path) {
  const QFileInfo info(path);
  QString resolved = info.canonicalFilePath();
  if (resolved.isEmpty()) resolved = info.absoluteFilePath();
  return QDir::toNativeSeparators(resolved).toLower();
}

std::optional<QString> aumidForWindow(WindowHandle hwnd, std::uint32_t pid) {
  const kernel::ComScope com(kernel::ComApartment::MultiThreaded);
  IPropertyStore* store = nullptr;
  if (SUCCEEDED(SHGetPropertyStoreForWindow(toHwnd(hwnd), IID_PPV_ARGS(&store))) && store) {
    PROPVARIANT value;
    PropVariantInit(&value);
    std::optional<QString> result;
    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal && *value.pwszVal) {
      result = fromWide(value.pwszVal);
    }
    PropVariantClear(&value);
    store->Release();
    if (result) return result;
  }
  return processAumid(pid);
}

std::optional<QString> explorerLocation(WindowHandle hwnd) {
  const kernel::ComScope com(kernel::ComApartment::MultiThreaded);
  IShellWindows* windows = nullptr;
  if (FAILED(
          CoCreateInstance(__uuidof(ShellWindows), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))) ||
      !windows) {
    return std::nullopt;
  }
  std::optional<QString> result;
  long count = 0;
  if (SUCCEEDED(windows->get_Count(&count))) {
    for (long index = 0; index < count && !result; ++index) {
      VARIANT item;
      VariantInit(&item);
      item.vt = VT_I4;
      item.lVal = index;
      IDispatch* dispatch = nullptr;
      if (FAILED(windows->Item(item, &dispatch)) || !dispatch) continue;
      IWebBrowser2* browser = nullptr;
      if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&browser))) && browser) {
        SHANDLE_PTR browserWindow = 0;
        if (SUCCEEDED(browser->get_HWND(&browserWindow)) &&
            static_cast<WindowHandle>(browserWindow) == hwnd) {
          BSTR url = nullptr;
          if (SUCCEEDED(browser->get_LocationURL(&url)) && url && *url) {
            const QString location = QString::fromWCharArray(url);
            if (location.startsWith(QLatin1String("file:///"))) {
              result =
                  QDir::toNativeSeparators(QUrl::fromPercentEncoding(location.mid(8).toUtf8()));
            } else {
              result = location;
            }
          }
          if (url) SysFreeString(url);
          if (!result) result = documentFolderPath(browser);
        }
        browser->Release();
      }
      dispatch->Release();
    }
  }
  windows->Release();
  return result;
}

std::vector<QString> normalizedTargets(const QString& exePath) {
  if (exePath.isEmpty()) return {};
  const QFileInfo info(exePath);
  const QString normalized = normalizePath(exePath);
  const QString parentName = info.dir().dirName();
  // Lexical, the directories need not exist.
  const QString grandparent = QDir::cleanPath(info.absolutePath() + QStringLiteral("/.."));
  const bool hasGrandparent = grandparent != QDir::cleanPath(info.absolutePath());

  // Electron: ...\app-1.2.3\app.exe -> ...\app.exe
  if (parentName.startsWith(QLatin1String("app-"), Qt::CaseInsensitive) && hasGrandparent) {
    return {normalized,
            QDir::toNativeSeparators(grandparent + QLatin1Char('/') + info.fileName()).toLower()};
  }
  // Scoop: ...\scoop\apps\name\1.2.3\app.exe -> ...\scoop\apps\name\app.exe
  static const QRegularExpression version(QStringLiteral("^\\d+[\\d.]*$"));
  const QString lowered = QDir::toNativeSeparators(exePath).toLower();
  if (hasGrandparent && version.match(parentName).hasMatch() &&
      lowered.contains(QLatin1String("\\scoop\\apps\\"))) {
    return {normalized,
            QDir::toNativeSeparators(grandparent + QLatin1Char('/') + info.fileName()).toLower()};
  }
  return {normalized};
}

QString commandLineArguments(const QString& commandLine) {
  const QString text = commandLine.trimmed();
  if (text.startsWith(QLatin1Char('"'))) {
    const qsizetype end = text.indexOf(QLatin1Char('"'), 1);
    return end >= 0 ? text.mid(end + 1).trimmed() : QString();
  }
  const qsizetype space = text.indexOf(QRegularExpression(QStringLiteral("\\s")));
  return space >= 0 ? text.mid(space + 1).trimmed() : QString();
}

std::optional<QString> hostArguments(const QString& exePath, std::uint32_t pid) {
  if (pid == 0) return std::nullopt;
  using NtQueryInformationProcessFn = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
  if (!query) return std::nullopt;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return std::nullopt;
  constexpr ULONG kProcessCommandLineInformation = 60;
  std::optional<QString> result;
  ULONG length = 0;
  query(process, kProcessCommandLineInformation, nullptr, 0, &length);
  if (length > 0) {
    std::vector<char> buffer(length);
    if (query(process, kProcessCommandLineInformation, buffer.data(), length, &length) >= 0) {
      // A UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }.
      const auto* text = reinterpret_cast<const UNICODE_STRING*>(buffer.data());
      if (text->Buffer && text->Length > 0) {
        result = commandLineArguments(
            QString::fromWCharArray(text->Buffer, static_cast<int>(text->Length / 2)));
      }
    }
  }
  CloseHandle(process);
  Q_UNUSED(exePath);
  return result;
}

std::optional<QString> appUniqueId(WindowHandle hwnd, const QString& exePath, std::uint32_t pid) {
  if (const auto aumid = aumidForWindow(hwnd, pid)) return QStringLiteral("aumid:") + *aumid;

  const bool isExplorer = exePath.contains(QLatin1String("explorer.exe"), Qt::CaseInsensitive);
  if (isExplorer) {
    if (const auto location = explorerLocation(hwnd); location && !location->isEmpty())
      return QStringLiteral("explorer:") + *location;
  }
  if (exePath.isEmpty()) return std::nullopt;

  const std::vector<QString> targets = normalizedTargets(exePath);
  const QString stable = targets.size() > 1 ? targets[1] : exePath;
  const bool isHost = exePath.endsWith(QLatin1String("mmc.exe"), Qt::CaseInsensitive) ||
                      exePath.endsWith(QLatin1String("rundll32.exe"), Qt::CaseInsensitive);
  if (isHost) {
    const auto args = hostArguments(exePath, pid);
    if (args && !args->isEmpty())
      return QStringLiteral("path:") + stable + QLatin1Char('|') + *args;
  }
  return QStringLiteral("path:") + stable;
}

}  // namespace wabar::taskbar

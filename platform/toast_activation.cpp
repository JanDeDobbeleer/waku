#include "platform/toast_activation.h"

// clang-format off
// The Windows SDK headers below depend on each other's order (propkey.h needs propsys.h).
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <NotificationActivationCallback.h>
// clang-format on

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/base.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <optional>
#include <string>
#include <vector>

#include "platform/media_session.h"
#include "platform/shell_open.h"

namespace yasb::platform {
namespace {

// {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 26 (propkey.h does not define it).
const PROPERTYKEY kToastActivatorClsid = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 26};

std::wstring wide(const QString& text) { return text.toStdWString(); }

std::optional<CLSID> parseClsid(const QString& text) {
  CLSID clsid{};
  if (SUCCEEDED(CLSIDFromString(wide(text.trimmed()).c_str(), &clsid))) return clsid;
  return std::nullopt;
}

QString toString(const CLSID& clsid) {
  wchar_t buffer[64]{};
  StringFromGUID2(clsid, buffer, 64);
  return QString::fromWCharArray(buffer);
}

// HKCR\AppUserModelId\<aumid>\CustomActivator.
std::optional<CLSID> registryActivator(const QString& aumid) {
  wchar_t value[128]{};
  DWORD size = sizeof(value);
  const std::wstring key = L"AppUserModelId\\" + wide(aumid);
  if (RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), L"CustomActivator", RRF_RT_REG_SZ, nullptr,
                   value, &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  return parseClsid(QString::fromWCharArray(value));
}

QStringList startMenuRoots() {
  QStringList roots;
  for (const char* variable : {"APPDATA", "PROGRAMDATA"}) {
    const QString base = qEnvironmentVariable(variable);
    if (base.isEmpty()) continue;
    const QString root = base + QStringLiteral("/Microsoft/Windows/Start Menu/Programs");
    if (QDir(root).exists()) roots << root;
  }
  return roots;
}

// The toast activator on the Start menu shortcut that carries this AUMID.
std::optional<CLSID> shortcutActivator(const QString& aumid) {
  for (const QString& root : startMenuRoots()) {
    QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const std::wstring path = wide(QDir::toNativeSeparators(it.next()));
      IPropertyStore* store = nullptr;
      if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT,
                                                   IID_PPV_ARGS(&store))) ||
          !store) {
        continue;
      }
      std::optional<CLSID> result;
      bool matches = false;
      PROPVARIANT id;
      PropVariantInit(&id);
      if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &id)) && id.vt == VT_LPWSTR &&
          id.pwszVal) {
        matches = QString::fromWCharArray(id.pwszVal) == aumid;
      }
      PropVariantClear(&id);
      if (matches) {
        PROPVARIANT clsid;
        PropVariantInit(&clsid);
        if (SUCCEEDED(store->GetValue(kToastActivatorClsid, &clsid)) && clsid.vt == VT_CLSID &&
            clsid.puuid) {
          result = *clsid.puuid;
        }
        PropVariantClear(&clsid);
      }
      store->Release();
      if (matches) return result;  // Python stops at the first shortcut with this AUMID
    }
  }
  return std::nullopt;
}

// The toast activator a packaged app declares for `appId` in its AppxManifest.xml.
std::optional<CLSID> manifestActivator(const QString& folder, const QString& appId) {
  if (folder.isEmpty() || appId.isEmpty()) return std::nullopt;
  QFile file(folder + QStringLiteral("/AppxManifest.xml"));
  if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
  QXmlStreamReader reader(&file);
  std::vector<QString> path;
  int appDepth = -1;  // depth of the matching <Application>
  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (token == QXmlStreamReader::StartElement) {
      const QString name = reader.name().toString();
      path.push_back(name);
      const int depth = static_cast<int>(path.size()) - 1;
      if (appDepth < 0 && name == QLatin1String("Application") &&
          reader.attributes().value(QStringLiteral("Id")) == appId) {
        appDepth = depth;
      } else if (appDepth >= 0 && depth == appDepth + 3 &&
                 name == QLatin1String("ToastNotificationActivation") &&
                 path[appDepth + 1] == QLatin1String("Extensions") &&
                 path[appDepth + 2] == QLatin1String("Extension")) {
        return parseClsid(
            QStringLiteral("{") +
            reader.attributes().value(QStringLiteral("ToastActivatorCLSID")).toString() +
            QStringLiteral("}"));
      }
    } else if (token == QXmlStreamReader::EndElement) {
      if (static_cast<int>(path.size()) - 1 == appDepth) return std::nullopt;
      if (!path.empty()) path.pop_back();
    }
  }
  return std::nullopt;
}

std::optional<CLSID> findActivator(const QString& aumid) {
  if (aumid.isEmpty()) return std::nullopt;
  if (auto clsid = registryActivator(aumid)) return clsid;
  if (auto clsid = shortcutActivator(aumid)) return clsid;
  return manifestActivator(packageInstallFolder(aumid), aumid.section(QLatin1Char('!'), 1));
}

bool openUri(const QString& launch) {
  if (!isOpenableToastUri(launch)) return false;
  AllowSetForegroundWindow(ASFW_ANY);
  shellOpen(launch);
  return true;
}

bool callActivator(const QString& aumid, const QString& launch) {
  const auto clsid = findActivator(aumid);
  if (!clsid) return false;
  // Out of process, an app's activator never loads into YASB.
  INotificationActivationCallback* activator = nullptr;
  const HRESULT created =
      CoCreateInstance(*clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&activator));
  if (FAILED(created) || !activator) {
    qWarning().noquote() << "The toast activator" << toString(*clsid) << "of" << aumid
                         << "could not be created";
    return false;
  }
  AllowSetForegroundWindow(ASFW_ANY);
  const HRESULT result = activator->Activate(wide(aumid).c_str(), wide(launch).c_str(), nullptr, 0);
  activator->Release();
  if (FAILED(result)) {
    qWarning().noquote() << "The toast activator" << toString(*clsid) << "of" << aumid
                         << "failed:" << Qt::hex << static_cast<unsigned long>(result);
    return false;
  }
  return true;
}

// aumid_process.resolve_shell_app()[0]: the Start menu name of the app, empty when it has none.
bool hasShellApp(const QString& aumid) {
  const std::wstring path = wide(QStringLiteral("shell:AppsFolder\\") + aumid);
  IShellItem* item = nullptr;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item)
    return false;
  bool named = false;
  PWSTR name = nullptr;
  if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
    named = !QString::fromWCharArray(name).trimmed().isEmpty();
    CoTaskMemFree(name);
  }
  item->Release();
  return named;
}

}  // namespace

bool isOpenableToastUri(const QString& launch) {
  static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
  return scheme.match(launch).hasMatch() &&
         !launch.startsWith(QLatin1String("file:"), Qt::CaseInsensitive);
}

bool activateToast(const QString& aumid, const QString& launch, const QString& activationType) {
  if (activationType == QLatin1String("protocol")) return openUri(launch);
  if (activationType == QLatin1String("foreground") ||
      activationType == QLatin1String("background")) {
    return callActivator(aumid, launch);
  }
  return false;
}

bool showApp(const QString& aumid) {
  if (aumid.isEmpty()) return false;
  if (activateAppByAumid(aumid)) return true;
  if (!hasShellApp(aumid)) return false;
  shellOpen(QStringLiteral("shell:AppsFolder\\") + aumid);
  return true;
}

QString packageInstallFolder(const QString& aumid) {
  // The listener's AppInfo does not implement Package (E_NOINTERFACE), a fresh lookup does.
  try {
    const auto info = winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(wide(aumid));
    if (!info) return {};
    return QString::fromStdWString(std::wstring(info.Package().InstalledPath()));
  } catch (const winrt::hresult_error& error) {
    qDebug().noquote() << "No install folder for" << aumid << ":"
                       << QString::fromWCharArray(error.message().c_str());
  } catch (...) {
  }
  return {};
}

}  // namespace yasb::platform

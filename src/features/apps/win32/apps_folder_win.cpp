#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl_core.h>

#include "features/apps/apps_platform.h"
#include "kernel/win32/com_scope.h"

namespace waku::apps {
namespace {

QString displayName(IShellItem* item, SIGDN kind) {
  PWSTR text = nullptr;
  QString result;
  if (SUCCEEDED(item->GetDisplayName(kind, &text)) && text) result = QString::fromWCharArray(text);
  CoTaskMemFree(text);
  return result;
}

// The packaged apps of shell:AppsFolder. The folder also lists every desktop app (their parsing
// name is a path or a plain AUMID), which the Start Menu shortcuts already cover; the AUMID of a
// packaged app is "<PackageFamilyName>!<AppId>", so only names with a "!" are kept.
std::vector<AppRecord> enumerateAppsFolder() {
  std::vector<AppRecord> apps;
  const kernel::ComScope com(kernel::ComApartment::MultiThreaded);
  IShellItem* folder = nullptr;
  if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                  IID_PPV_ARGS(&folder))) ||
      !folder) {
    return apps;
  }
  IEnumShellItems* items = nullptr;
  if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))) && items) {
    IShellItem* item = nullptr;
    ULONG fetched = 0;
    while (items->Next(1, &item, &fetched) == S_OK && fetched == 1) {
      const QString name = displayName(item, SIGDN_NORMALDISPLAY);
      const QString aumid = displayName(item, SIGDN_PARENTRELATIVEPARSING);
      item->Release();
      item = nullptr;
      if (name.isEmpty() || !aumid.contains(QLatin1Char('!'))) continue;
      AppRecord record;
      record.id = aumid;
      record.name = name;
      record.kind = AppKind::Store;
      record.aumid = aumid;
      record.iconSource = QStringLiteral("shell:AppsFolder\\") + aumid;
      apps.push_back(std::move(record));
    }
    items->Release();
  }
  folder->Release();
  return apps;
}

}  // namespace

AppsFolderEnumerator makeWin32AppsFolderEnumerator() { return &enumerateAppsFolder; }

}  // namespace waku::apps

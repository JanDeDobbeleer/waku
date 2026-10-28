#include "platform/dnd.h"

#include <windows.h>
#include <combaseapi.h>

#include <QDebug>
#include <QMetaObject>
#include <functional>

namespace yasb::platform {
namespace {

// Undocumented shell interface (docs/architecture.md allowlist). Only the two profile accessors
// are declared, in the order Python YASB's binding uses, directly after IUnknown.
struct __declspec(uuid("6BFF4732-81EC-4FFB-AE67-B6C1BC29631F")) IQuietHoursSettings : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE get_UserSelectedProfile(LPWSTR* profileId) = 0;
  virtual HRESULT STDMETHODCALLTYPE put_UserSelectedProfile(LPCWSTR profileId) = 0;
};

// {F53321FA-34F8-4B7F-B9A3-361877CB94CF}
const CLSID kClsidQuietHoursSettings = {
    0xF53321FA, 0x34F8, 0x4B7F, {0xB9, 0xA3, 0x36, 0x18, 0x77, 0xCB, 0x94, 0xCF}};

struct ComScope {
  ComScope() : initialised(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
  ~ComScope() {
    if (initialised) CoUninitialize();
  }
  bool initialised;
};

struct Profile {
  const wchar_t* id;
  const char* mode;
};

constexpr Profile kProfiles[] = {
    {L"Microsoft.QuietHoursProfile.Unrestricted", "disabled"},
    {L"Microsoft.QuietHoursProfile.PriorityOnly", "priority"},
    {L"Microsoft.QuietHoursProfile.AlarmsOnly", "alarms"},
};

// The settings object lives only inside this helper so it is released before CoUninitialize.
bool withSettings(const std::function<bool(IQuietHoursSettings*)>& use) {
  IQuietHoursSettings* settings = nullptr;
  const HRESULT hr =
      CoCreateInstance(kClsidQuietHoursSettings, nullptr, CLSCTX_LOCAL_SERVER,
                       __uuidof(IQuietHoursSettings), reinterpret_cast<void**>(&settings));
  if (FAILED(hr) || !settings) return false;
  const bool ok = use(settings);
  settings->Release();
  return ok;
}

}  // namespace

QString dndStatus() {
  ComScope com;
  QString profile;
  withSettings([&](IQuietHoursSettings* settings) {
    LPWSTR raw = nullptr;
    if (FAILED(settings->get_UserSelectedProfile(&raw)) || !raw) return false;
    profile = QString::fromWCharArray(raw);
    CoTaskMemFree(raw);
    return true;
  });
  for (const Profile& entry : kProfiles) {
    if (profile == QString::fromWCharArray(entry.id)) return QString::fromLatin1(entry.mode);
  }
  return QStringLiteral("unknown");
}

bool setDndStatus(const QString& mode) {
  const wchar_t* id = nullptr;
  for (const Profile& entry : kProfiles) {
    if (mode == QLatin1String(entry.mode)) id = entry.id;
  }
  if (!id) {
    qWarning().noquote() << "Invalid DND mode" << mode;
    return false;
  }
  ComScope com;
  return withSettings([&](IQuietHoursSettings* settings) {
    return SUCCEEDED(settings->put_UserSelectedProfile(id));
  });
}

namespace {

// Undocumented WNF state name (docs/architecture.md allowlist).
constexpr ULONGLONG kWnfQuietHoursActiveProfileChanged = 0x0D83063EA3BF1C75ull;

using WnfCallback = LONG(NTAPI*)(ULONGLONG stateName, ULONG changeStamp, void* typeId,
                                 void* context, const void* buffer, ULONG bufferSize);
using RtlSubscribeFn = LONG(NTAPI*)(void** subscription, ULONGLONG stateName, ULONG changeStamp,
                                    WnfCallback callback, void* context, void* typeId,
                                    ULONG serializationGroup, ULONG unknown);
using RtlUnsubscribeFn = LONG(NTAPI*)(void* subscription);

RtlUnsubscribeFn g_unsubscribe = nullptr;

LONG NTAPI onQuietHoursChanged(ULONGLONG, ULONG, void*, void* context, const void*, ULONG) {
  auto* watcher = static_cast<DndWatcher*>(context);
  if (!watcher) return 0;
  try {
    const QString status = dndStatus();
    QMetaObject::invokeMethod(
        watcher, [watcher, status] { watcher->deliver(status); }, Qt::QueuedConnection);
  } catch (...) {
    // Never let an exception cross into the system thread.
  }
  return 0;
}

}  // namespace

DndWatcher::DndWatcher(QObject* parent) : QObject(parent) {}

DndWatcher::~DndWatcher() { stop(); }

bool DndWatcher::start() {
  if (active_) return true;
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return false;
  const auto subscribe = reinterpret_cast<RtlSubscribeFn>(
      GetProcAddress(ntdll, "RtlSubscribeWnfStateChangeNotification"));
  g_unsubscribe = reinterpret_cast<RtlUnsubscribeFn>(
      GetProcAddress(ntdll, "RtlUnsubscribeWnfStateChangeNotification"));
  if (!subscribe || !g_unsubscribe) {
    qWarning() << "WNF is not available, the Do Not Disturb button updates only on its own clicks";
    return false;
  }
  void* subscription = nullptr;
  const LONG status = subscribe(&subscription, kWnfQuietHoursActiveProfileChanged, 0,
                                &onQuietHoursChanged, this, nullptr, 0, 0);
  if (status != 0) {
    qWarning().noquote() << "DndService failed with status" << Qt::hex
                         << static_cast<ULONG>(status);
    return false;
  }
  subscription_ = subscription;
  active_ = true;
  qInfo() << "DndService initialized";
  return true;
}

void DndWatcher::stop() {
  if (!active_ || !subscription_ || !g_unsubscribe) return;
  g_unsubscribe(subscription_);
  subscription_ = nullptr;
  active_ = false;
}

void DndWatcher::deliver(const QString& status) { emit statusChanged(status); }

}  // namespace yasb::platform

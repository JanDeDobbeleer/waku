#include <windows.h>
#include <combaseapi.h>

#include <QDebug>
#include <QMetaObject>
#include <functional>

#include "features/notifications/dnd.h"
#include "features/notifications/win32/wnf_win.h"
#include "kernel/win32/com_scope.h"

namespace waku::notifications {
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
  const kernel::ComScope com(kernel::ComApartment::SingleThreaded);
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
  const kernel::ComScope com(kernel::ComApartment::SingleThreaded);
  return withSettings([&](IQuietHoursSettings* settings) {
    return SUCCEEDED(settings->put_UserSelectedProfile(id));
  });
}

namespace {

// Undocumented WNF state name (docs/architecture.md allowlist).
constexpr ULONGLONG kWnfQuietHoursActiveProfileChanged = 0x0D83063EA3BF1C75ull;

}  // namespace

DndWatcher::DndWatcher(QObject* parent) : QObject(parent) {}

DndWatcher::~DndWatcher() { stop(); }

bool DndWatcher::start() {
  if (wnf_ && wnf_->active()) return true;
  wnf_ = std::make_unique<WnfSubscription>();
  if (!wnf_->available()) {
    qWarning() << "WNF is not available, the Do Not Disturb button updates only on its own clicks";
    wnf_.reset();
    return false;
  }
  // WNF system thread. The subscription runs this under its lock and stop() waits for it, so
  // `this` is alive for the whole call; the post is dropped if the watcher dies before delivery.
  const long status =
      wnf_->subscribe(kWnfQuietHoursActiveProfileChanged, [this](const void*, unsigned long) {
        const QString status = dndStatus();
        QMetaObject::invokeMethod(this, [this, status] { deliver(status); }, Qt::QueuedConnection);
      });
  if (status != 0) {
    qWarning().noquote() << "DndService failed with status" << Qt::hex
                         << static_cast<ULONG>(status);
    wnf_.reset();
    return false;
  }
  qInfo() << "DndService initialized";
  return true;
}

void DndWatcher::stop() { wnf_.reset(); }

void DndWatcher::deliver(const QString& status) { emit statusChanged(status); }

}  // namespace waku::notifications

// The shell interface behind Task View. Every GUID, vtable slot and struct below is an
// undocumented detail of explorer.exe; the allowlist row in docs/architecture.md lists what was
// verified on which build, and the "Shell interface" section of features/workspaces/design.md
// explains the fail-soft chain. The interfaces are declared here from those facts alone.
#include "features/workspaces/win32/shell_desktops_win.h"

#include <Windows.h>
#include <ObjectArray.h>
#include <ServProv.h>
#include <winstring.h>
#include <wrl/client.h>

#include <QDebug>
#include <QPointer>
#include <QWinEventNotifier>
#include <atomic>
#include <memory>
#include <set>
#include <vector>

#include "features/workspaces/virtual_desktops.h"
#include "kernel/win32/com_scope.h"
#include "kernel/win32/windows_build.h"

namespace waku::workspaces::vdesk {

std::optional<QByteArray> readVirtualDesktopsValue(const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kVirtualDesktopsKey, 0, KEY_QUERY_VALUE, &key) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  DWORD type = 0;
  DWORD size = 0;
  std::optional<QByteArray> result;
  if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
      type == REG_BINARY && size != 0 && size <= 64 * 1024) {
    QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(bytes.data()), &size) ==
        ERROR_SUCCESS) {
      bytes.resize(static_cast<qsizetype>(size));
      result = bytes;
    }
  }
  RegCloseKey(key);
  return result;
}

// ---- RegistryChangeWatcher -------------------------------------------------------------------

struct RegistryChangeWatcher::Impl {
  HKEY key = nullptr;
  HANDLE event = nullptr;
  std::unique_ptr<QWinEventNotifier> notifier;
  std::function<void()> onChange;

  ~Impl() {
    notifier.reset();
    if (key) RegCloseKey(key);
    if (event) CloseHandle(event);
  }

  bool arm() {
    return RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_NAME,
                                   event, TRUE) == ERROR_SUCCESS;
  }
};

RegistryChangeWatcher::RegistryChangeWatcher() = default;
RegistryChangeWatcher::~RegistryChangeWatcher() = default;

bool RegistryChangeWatcher::start(std::function<void()> onChange) {
  if (impl_) return true;
  auto impl = std::make_unique<Impl>();
  impl->onChange = std::move(onChange);
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kVirtualDesktopsKey, 0, KEY_NOTIFY | KEY_QUERY_VALUE,
                    &impl->key) != ERROR_SUCCESS) {
    return false;
  }
  impl->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
  if (!impl->event || !impl->arm()) return false;
  Impl* raw = impl.get();
  impl->notifier = std::make_unique<QWinEventNotifier>(impl->event);
  QObject::connect(impl->notifier.get(), &QWinEventNotifier::activated, impl->notifier.get(),
                   [raw] {
                     try {
                       ResetEvent(raw->event);
                       if (!raw->arm()) {
                         qWarning() << "virtual desktops: the registry watch was lost";
                         raw->notifier->setEnabled(false);
                       }
                       if (raw->onChange) raw->onChange();
                     } catch (...) {
                       qWarning() << "virtual desktops: registry change handler threw";
                     }
                   });
  impl_ = std::move(impl);
  return true;
}

// ---- COM declarations ------------------------------------------------------------------------

namespace {

constexpr GUID kClsidImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
constexpr GUID kServiceManager = {
    0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};
constexpr GUID kServiceNotification = {
    0xA501FDEC, 0x4A09, 0x464C, {0xAE, 0x4E, 0x1B, 0x9C, 0x21, 0xB8, 0x49, 0x18}};
constexpr GUID kIidManager = {
    0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}};
constexpr GUID kIidDesktop = {
    0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}};
constexpr GUID kIidNotificationService = {
    0x0CD45E71, 0xD927, 0x4F15, {0x8B, 0x0A, 0x8F, 0xEF, 0x52, 0x53, 0x37, 0xBF}};
constexpr GUID kIidNotification = {
    0xB9E5E94D, 0x233E, 0x49AB, {0xAF, 0x5C, 0x2B, 0x45, 0x41, 0xC3, 0xAA, 0xDE}};

// IVirtualDesktop on build 26100 and later has these four methods and no more (a fifth, IsRemote,
// is not there and calling it crashes). Slots count from 3, after IUnknown.
struct IShellDesktop : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE IsViewVisible(void* view, int* visible) = 0;  // 3
  virtual HRESULT STDMETHODCALLTYPE GetId(GUID* id) = 0;                          // 4
  virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* name) = 0;                   // 5
  virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING* path) = 0;          // 6
};

// IVirtualDesktopManagerInternal, IID 53F5CA0B. Unused slots keep their place.
struct IShellDesktopManager : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;                       // 3
  virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(void*, IShellDesktop*) = 0;    // 4
  virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(void*, int*) = 0;            // 5
  virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IShellDesktop** desktop) = 0;  // 6
  virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray** desktops) = 0;        // 7
  virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IShellDesktop*, UINT,
                                                       IShellDesktop**) = 0;                 // 8
  virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IShellDesktop* desktop) = 0;               // 9
  virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView(IShellDesktop*) = 0;  // 10
  virtual HRESULT STDMETHODCALLTYPE CreateDesktop(IShellDesktop** desktop) = 0;              // 11
  virtual HRESULT STDMETHODCALLTYPE MoveDesktop(IShellDesktop*, UINT) = 0;                   // 12
  virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(IShellDesktop* desktop,
                                                  IShellDesktop* fallback) = 0;                // 13
  virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID* id, IShellDesktop** desktop) = 0;  // 14
  virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews(IShellDesktop*,
                                                                        IObjectArray**,
                                                                        IObjectArray**) = 0;   // 15
  virtual HRESULT STDMETHODCALLTYPE SetDesktopName(IShellDesktop* desktop, HSTRING name) = 0;  // 16
  virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(IShellDesktop*, HSTRING) = 0;          // 17
  virtual HRESULT STDMETHODCALLTYPE UpdateWallpaperPathForAllDesktops(HSTRING) = 0;            // 18
  virtual HRESULT STDMETHODCALLTYPE CopyDesktopState(void*, void*) = 0;                        // 19
  virtual HRESULT STDMETHODCALLTYPE CreateRemoteDesktop(HSTRING, IShellDesktop**) = 0;         // 20
  virtual HRESULT STDMETHODCALLTYPE SwitchRemoteDesktop(IShellDesktop*, void*) = 0;            // 21
  virtual HRESULT STDMETHODCALLTYPE SwitchDesktopWithAnimation(IShellDesktop* desktop) = 0;    // 22
};

// IVirtualDesktopNotificationService, IID 0CD45E71.
struct IShellDesktopNotificationService : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Register(IUnknown* sink, DWORD* cookie) = 0;  // 3
  virtual HRESULT STDMETHODCALLTYPE Unregister(DWORD cookie) = 0;                 // 4
};

// IVirtualDesktopNotification, IID B9E5E94D: the methods after IUnknown, in this order.
struct IShellDesktopNotification : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(IShellDesktop*) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(IShellDesktop* destroyed,
                                                               IShellDesktop* fallback) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(IShellDesktop*, IShellDesktop*) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(IShellDesktop*, IShellDesktop*) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(IShellDesktop*, INT64, INT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(IShellDesktop*, HSTRING) = 0;
  virtual HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(IShellDesktop*,
                                                                 IShellDesktop*) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(IShellDesktop*, HSTRING) = 0;
  virtual HRESULT STDMETHODCALLTYPE VirtualDesktopSwitched(IShellDesktop*) = 0;
  virtual HRESULT STDMETHODCALLTYPE RemoteVirtualDesktopConnected(IShellDesktop*) = 0;
};

using Microsoft::WRL::ComPtr;

constexpr HRESULT kFault = static_cast<HRESULT>(0x8000FFFF);  // E_UNEXPECTED: SEH or C++ exception

// A free function without objects needing unwinding, so __try is allowed. A wrong slot would call
// another method of the shell; if the answer is a crash inside our process it ends here.
template <class F>
HRESULT sehInvoke(F& f) {
  __try {
    return f();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return kFault;
  }
}

template <class F>
HRESULT guardedCall(F f) {
  try {
    return sehInvoke(f);
  } catch (...) {
    return kFault;
  }
}

bool connectionLost(HRESULT hr) {
  return hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) ||
         hr == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED) ||
         hr == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED_DNE) || hr == RPC_E_DISCONNECTED ||
         hr == RPC_E_SERVER_DIED || hr == RPC_E_SERVER_DIED_DNE;
}

QString hex(HRESULT hr) {
  return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

QString idString(const GUID& guid) {
  wchar_t text[64] = {};
  if (StringFromGUID2(guid, text, 64) == 0) return {};
  return QString::fromWCharArray(text).toUpper();
}

std::optional<GUID> parseId(const QString& id) {
  GUID guid = {};
  if (CLSIDFromString(id.toStdWString().c_str(), &guid) != S_OK) return std::nullopt;
  return guid;
}

std::optional<QString> idOf(IShellDesktop* desktop) {
  GUID guid = {};
  if (!desktop || FAILED(guardedCall([&] { return desktop->GetId(&guid); }))) return std::nullopt;
  return idString(guid);
}

bool interfaceRegistered(const GUID& iid) {
  const std::wstring path = L"Interface\\" + idString(iid).toStdWString();
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  RegCloseKey(key);
  return true;
}

// The notification sink. Windows calls it on the thread that registered it, while that thread is
// inside an outbound call or pumping messages. A handler must not call into the manager: it only
// posts a queued call to the GUI thread and returns.
class Sink final : public IShellDesktopNotification {
 public:
  Sink(QObject* context, std::function<void()> changed,
       std::function<void(QString, QString)> destroyBegin)
      : context_(context),
        changed_(std::make_shared<std::function<void()>>(std::move(changed))),
        destroyBegin_(
            std::make_shared<std::function<void(QString, QString)>>(std::move(destroyBegin))) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (riid == IID_IUnknown || riid == kIidNotification) {
      *out = static_cast<IShellDesktopNotification*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --refs_;
    if (left == 0) delete this;
    return left;
  }

  HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(IShellDesktop*) override { return postChanged(); }
  HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(IShellDesktop* destroyed,
                                                       IShellDesktop* fallback) override {
    try {
      QObject* context = context_.data();
      if (context && destroyed && fallback) {
        // The ids are read later, on the GUI thread, outside this handler.
        ComPtr<IShellDesktop> a(destroyed);
        ComPtr<IShellDesktop> b(fallback);
        auto callback = destroyBegin_;
        QMetaObject::invokeMethod(
            context,
            [a, b, callback] {
              try {
                const std::optional<QString> from = idOf(a.Get());
                const std::optional<QString> to = idOf(b.Get());
                if (*callback) (*callback)(from.value_or(QString()), to.value_or(QString()));
              } catch (...) {
              }
            },
            Qt::QueuedConnection);
      }
    } catch (...) {
    }
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(IShellDesktop*, IShellDesktop*) override {
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(IShellDesktop*, IShellDesktop*) override {
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(IShellDesktop*, INT64, INT64) override {
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(IShellDesktop*, HSTRING) override {
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(void*) override { return postChanged(); }
  HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(IShellDesktop*, IShellDesktop*) override {
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(IShellDesktop*, HSTRING) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE VirtualDesktopSwitched(IShellDesktop*) override {
    return postChanged();
  }
  HRESULT STDMETHODCALLTYPE RemoteVirtualDesktopConnected(IShellDesktop*) override { return S_OK; }

 private:
  HRESULT postChanged() {
    try {
      QObject* context = context_.data();
      if (context) {
        auto callback = changed_;
        QMetaObject::invokeMethod(
            context,
            [callback] {
              try {
                if (*callback) (*callback)();
              } catch (...) {
              }
            },
            Qt::QueuedConnection);
      }
    } catch (...) {
    }
    return S_OK;
  }

  std::atomic<ULONG> refs_{1};
  QPointer<QObject> context_;
  std::shared_ptr<std::function<void()>> changed_;
  std::shared_ptr<std::function<void(QString, QString)>> destroyBegin_;
};

}  // namespace

// ---- ShellDesktopApi -------------------------------------------------------------------------

struct ShellDesktopApi::Impl {
  // Declared first, so it goes last: the COM pointers below are released before CoUninitialize.
  kernel::ComScope com{kernel::ComApartment::SingleThreaded};
  ComPtr<IServiceProvider> provider;
  ComPtr<IShellDesktopManager> manager;
  ComPtr<IShellDesktopNotificationService> notifications;
  ComPtr<Sink> sink;
  DWORD cookie = 0;
  QObject context;  // the target of the queued posts from the sink
  bool lost = false;
  bool verified = false;
  std::set<QString> logged;

  ~Impl() {
    if (notifications && cookie != 0) {
      guardedCall([&] { return notifications->Unregister(cookie); });
    }
  }

  template <class F>
  HRESULT call(const char* what, F f) {
    const HRESULT hr = guardedCall(std::move(f));
    if (FAILED(hr)) {
      if (connectionLost(hr)) lost = true;
      if (logged.insert(QString::fromLatin1(what)).second) {
        qWarning().noquote() << "virtual desktops: shell call failed:" << what << hex(hr);
      }
    }
    return hr;
  }

  std::vector<ComPtr<IShellDesktop>> desktops() {
    std::vector<ComPtr<IShellDesktop>> out;
    ComPtr<IObjectArray> array;
    if (FAILED(call("GetDesktops", [&] { return manager->GetDesktops(array.GetAddressOf()); })) ||
        !array) {
      return out;
    }
    UINT count = 0;
    if (FAILED(call("IObjectArray::GetCount", [&] { return array->GetCount(&count); }))) {
      return out;
    }
    for (UINT i = 0; i < count; ++i) {
      ComPtr<IShellDesktop> desktop;
      if (FAILED(call("IObjectArray::GetAt",
                      [&] {
                        return array->GetAt(i, kIidDesktop,
                                            reinterpret_cast<void**>(desktop.GetAddressOf()));
                      })) ||
          !desktop) {
        return {};
      }
      out.push_back(std::move(desktop));
    }
    return out;
  }

  ComPtr<IShellDesktop> find(const QString& id) {
    ComPtr<IShellDesktop> desktop;
    const std::optional<GUID> guid = parseId(id);
    if (!guid) return desktop;
    if (FAILED(call("FindDesktop",
                    [&] { return manager->FindDesktop(&*guid, desktop.GetAddressOf()); }))) {
      desktop.Reset();
    }
    return desktop;
  }

  std::optional<QStringList> ids() {
    const std::vector<ComPtr<IShellDesktop>> list = desktops();
    if (list.empty()) return std::nullopt;
    QStringList out;
    for (const ComPtr<IShellDesktop>& desktop : list) {
      const std::optional<QString> id = [&] {
        GUID guid = {};
        if (FAILED(call("IVirtualDesktop::GetId", [&] { return desktop->GetId(&guid); }))) {
          return std::optional<QString>();
        }
        return std::optional<QString>(idString(guid));
      }();
      if (!id) return std::nullopt;
      out << *id;
    }
    return out;
  }

  std::optional<QString> current() {
    ComPtr<IShellDesktop> desktop;
    if (FAILED(call("GetCurrentDesktop",
                    [&] { return manager->GetCurrentDesktop(desktop.GetAddressOf()); })) ||
        !desktop) {
      return std::nullopt;
    }
    GUID guid = {};
    if (FAILED(call("IVirtualDesktop::GetId", [&] { return desktop->GetId(&guid); }))) {
      return std::nullopt;
    }
    return idString(guid);
  }

  // Read-only comparison with the registry. Empty on success, else the first difference.
  QString compareWithRegistry() {
    const std::optional<QByteArray> rawIds = readVirtualDesktopsValue(L"VirtualDesktopIDs");
    const QStringList registry = rawIds ? parseDesktopIds(*rawIds) : QStringList();
    if (registry.isEmpty()) return QStringLiteral("the registry desktop list is empty");
    UINT count = 0;
    if (FAILED(call("GetCount", [&] { return manager->GetCount(&count); }))) {
      return QStringLiteral("GetCount failed");
    }
    if (static_cast<qsizetype>(count) != registry.size()) {
      return QStringLiteral("GetCount %1 differs from the registry (%2)")
          .arg(count)
          .arg(registry.size());
    }
    const std::optional<QStringList> listed = ids();
    if (!listed) return QStringLiteral("GetDesktops failed");
    if (*listed != registry) return QStringLiteral("GetDesktops differs from the registry list");
    const std::optional<QByteArray> rawCurrent = readVirtualDesktopsValue(L"CurrentVirtualDesktop");
    const QStringList shown = rawCurrent ? parseDesktopIds(*rawCurrent) : QStringList();
    const std::optional<QString> now = current();
    if (!now || shown.isEmpty() || *now != shown.first()) {
      return QStringLiteral("GetCurrentDesktop differs from the registry");
    }
    const ComPtr<IShellDesktop> last = find(registry.last());
    const std::optional<QString> lastId = last ? idOf(last.Get()) : std::nullopt;
    if (!lastId || *lastId != registry.last()) {
      return QStringLiteral("FindDesktop does not round-trip");
    }
    return {};
  }

  // The slots of the mutating calls were never run on a verified build. Before the first one of a
  // run, the read-only slots are compared with the registry again, so a shifted vtable shows up as
  // a mismatch instead of closing the wrong desktop.
  bool verifiedForMutation() {
    if (verified) return true;
    const QString problem = compareWithRegistry();
    if (!problem.isEmpty()) {
      qWarning().noquote() << "virtual desktops: shell api refused to change desktops:" << problem;
      return false;
    }
    qDebug().noquote() << "virtual desktops: shell api re-verified before the first change";
    verified = true;
    return true;
  }
};

ShellDesktopApi::ShellDesktopApi(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ShellDesktopApi::~ShellDesktopApi() = default;

bool ShellDesktopApi::lost() const { return impl_->lost; }

std::unique_ptr<ShellDesktopApi> ShellDesktopApi::open(QString* reason) {
  auto fail = [reason](const QString& why) {
    if (reason) *reason = why;
    return std::unique_ptr<ShellDesktopApi>();
  };
  try {
    const unsigned build = kernel::windowsBuild();
    if (build < 26100) {
      return fail(QStringLiteral("Windows build %1 is older than 26100").arg(build));
    }
    if (!interfaceRegistered(kIidManager) || !interfaceRegistered(kIidDesktop)) {
      return fail(QStringLiteral("the interfaces are not registered on this build"));
    }
    auto impl = std::make_unique<Impl>();
    HRESULT hr = impl->call("CoCreateInstance(ImmersiveShell)", [&] {
      return CoCreateInstance(kClsidImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER,
                              __uuidof(IServiceProvider),
                              reinterpret_cast<void**>(impl->provider.GetAddressOf()));
    });
    if (FAILED(hr) || !impl->provider) return fail(QStringLiteral("ImmersiveShell: ") + hex(hr));
    ComPtr<IUnknown> unknown;
    hr = impl->call("QueryService(manager)", [&] {
      return impl->provider->QueryService(kServiceManager, IID_IUnknown,
                                          reinterpret_cast<void**>(unknown.GetAddressOf()));
    });
    if (FAILED(hr) || !unknown) return fail(QStringLiteral("QueryService: ") + hex(hr));
    hr = impl->call("QueryInterface(manager)", [&] {
      return unknown->QueryInterface(kIidManager,
                                     reinterpret_cast<void**>(impl->manager.GetAddressOf()));
    });
    if (FAILED(hr) || !impl->manager) {
      return fail(QStringLiteral("the manager interface was refused: ") + hex(hr));
    }
    const QString problem = impl->compareWithRegistry();
    if (!problem.isEmpty()) return fail(QStringLiteral("self-test: ") + problem);
    impl->logged.clear();
    return std::unique_ptr<ShellDesktopApi>(new ShellDesktopApi(std::move(impl)));
  } catch (...) {
    return fail(QStringLiteral("an exception"));
  }
}

std::optional<QStringList> ShellDesktopApi::desktopIds() { return impl_->ids(); }

std::optional<QString> ShellDesktopApi::currentId() { return impl_->current(); }

bool ShellDesktopApi::switchTo(const QString& id, bool animate) {
  if (!impl_->verifiedForMutation()) return false;
  const ComPtr<IShellDesktop> desktop = impl_->find(id);
  if (!desktop) return false;
  IShellDesktopManager* manager = impl_->manager.Get();
  return SUCCEEDED(
      animate
          ? impl_->call("SwitchDesktopWithAnimation",
                        [&] { return manager->SwitchDesktopWithAnimation(desktop.Get()); })
          : impl_->call("SwitchDesktop", [&] { return manager->SwitchDesktop(desktop.Get()); }));
}

std::optional<QString> ShellDesktopApi::create() {
  if (!impl_->verifiedForMutation()) return std::nullopt;
  ComPtr<IShellDesktop> desktop;
  if (FAILED(impl_->call("CreateDesktop",
                         [&] { return impl_->manager->CreateDesktop(desktop.GetAddressOf()); })) ||
      !desktop) {
    return std::nullopt;
  }
  return idOf(desktop.Get());
}

bool ShellDesktopApi::remove(const QString& id, const QString& fallbackId) {
  if (id.isEmpty() || id == fallbackId || !impl_->verifiedForMutation()) return false;
  const ComPtr<IShellDesktop> desktop = impl_->find(id);
  const ComPtr<IShellDesktop> fallback = impl_->find(fallbackId);
  if (!desktop || !fallback) return false;
  return SUCCEEDED(impl_->call("RemoveDesktop", [&] {
    return impl_->manager->RemoveDesktop(desktop.Get(), fallback.Get());
  }));
}

bool ShellDesktopApi::rename(const QString& id, const QString& name) {
  if (!impl_->verifiedForMutation()) return false;
  const ComPtr<IShellDesktop> desktop = impl_->find(id);
  if (!desktop) return false;
  HSTRING text = nullptr;
  if (!name.isEmpty() && FAILED(WindowsCreateString(reinterpret_cast<const wchar_t*>(name.utf16()),
                                                    static_cast<UINT32>(name.size()), &text))) {
    return false;
  }
  const bool ok = SUCCEEDED(impl_->call(
      "SetDesktopName", [&] { return impl_->manager->SetDesktopName(desktop.Get(), text); }));
  if (text) WindowsDeleteString(text);
  return ok;
}

bool ShellDesktopApi::registerNotifications(std::function<void()> changed,
                                            std::function<void(QString, QString)> destroyBegin) {
  if (impl_->cookie != 0) return true;
  ComPtr<IUnknown> unknown;
  HRESULT hr = impl_->call("QueryService(notifications)", [&] {
    return impl_->provider->QueryService(kServiceNotification, IID_IUnknown,
                                         reinterpret_cast<void**>(unknown.GetAddressOf()));
  });
  if (FAILED(hr) || !unknown) return false;
  hr = impl_->call("QueryInterface(notifications)", [&] {
    return unknown->QueryInterface(kIidNotificationService,
                                   reinterpret_cast<void**>(impl_->notifications.GetAddressOf()));
  });
  if (FAILED(hr) || !impl_->notifications) return false;
  ComPtr<Sink> sink;
  sink.Attach(new Sink(&impl_->context, std::move(changed), std::move(destroyBegin)));
  DWORD cookie = 0;
  hr = impl_->call("Register", [&] { return impl_->notifications->Register(sink.Get(), &cookie); });
  if (FAILED(hr)) return false;
  impl_->sink = std::move(sink);
  impl_->cookie = cookie;
  return true;
}

}  // namespace waku::workspaces::vdesk

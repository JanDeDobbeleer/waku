// Windows notification count and toast list for NotificationService.
//
// Threading: the count comes from a WNF state subscription; its callback runs on a system thread
// and only forwards the value to the sink and arms a list refresh. Everything else (reading the
// list, remove, clear, activate) runs on one worker thread (MTA) that owns the
// UserNotificationListener; requests enqueue a task (or arm the debounced refresh) and the thread
// sleeps on a condition variable otherwise. stop() unsubscribes WNF (WnfSubscription returns only
// after a running callback has left; see wnf_win.h), wakes the worker and joins it.
#include <Windows.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <QDebug>
#include <QTimeZone>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "features/notifications/notifications.h"
#include "features/notifications/toast_activation.h"
#include "features/notifications/toast_xml.h"
#include "features/notifications/win32/wnf_win.h"

namespace wabar::notifications {
namespace {

namespace wun = winrt::Windows::UI::Notifications;
namespace wum = winrt::Windows::UI::Notifications::Management;

// Undocumented WNF state names (docs/architecture.md allowlist).
constexpr ULONGLONG kWnfShellNotificationTotal = 0x0D83063EA3B8D035ull;  // Windows 11 toast total
constexpr ULONGLONG kWnfShellNotifications = 0x0D83063EA3BC1035ull;      // Windows 10 unread badge

// Time to wait after a change, so a burst of changes is read once.
constexpr std::chrono::milliseconds kRefreshDelay{100};
// Where Windows records the web images it downloaded for toasts.
constexpr wchar_t kWpnidmKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications\\wpnidm";

QString toQString(const winrt::hstring& text) {
  return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

std::wstring registryString(HKEY key, const wchar_t* name) {
  DWORD size = 0;
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
      size < sizeof(wchar_t)) {
    return {};
  }
  std::wstring value(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) !=
      ERROR_SUCCESS) {
    return {};
  }
  value.resize(wcsnlen(value.c_str(), value.size()));
  return value;
}

// The file Windows saved for a web image, empty when it has none (_get_downloaded_image).
QString downloadedImage(const QString& url) {
  HKEY root = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kWpnidmKey, 0, KEY_READ, &root) != ERROR_SUCCESS) return {};
  QString result;
  for (DWORD index = 0;; ++index) {
    wchar_t name[256];
    DWORD length = 256;
    if (RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr) !=
        ERROR_SUCCESS) {
      break;
    }
    HKEY entry = nullptr;
    if (RegOpenKeyExW(root, name, 0, KEY_READ, &entry) != ERROR_SUCCESS) continue;
    QString stored = QString::fromStdWString(registryString(entry, L"Url"));
    if (stored.startsWith(QLatin1String("wpnidm:"))) stored.remove(0, 7);
    if (stored == url) result = QString::fromStdWString(registryString(entry, L"LocalPath"));
    RegCloseKey(entry);
    if (!result.isEmpty()) break;
  }
  RegCloseKey(root);
  return result;
}

ImagePathEnv imageEnv(const QString& aumid, const QString& family) {
  ImagePathEnv env;
  env.family = family;
  env.localAppData = qEnvironmentVariable("LOCALAPPDATA");
  env.installFolder = [aumid] { return packageInstallFolder(aumid); };
  env.downloadedImage = downloadedImage;
  return env;
}

struct AppInfoData {
  QString aumid;
  QString name;
  QString family;
};

// AUMID, display name and package family name, empty for a sender Windows cannot name.
AppInfoData readAppInfo(const wun::UserNotification& notification) {
  try {
    const auto app = notification.AppInfo();
    return {toQString(app.AppUserModelId()), toQString(app.DisplayInfo().DisplayName()),
            toQString(app.PackageFamilyName())};
  } catch (const winrt::hresult_error&) {
    // senders without a registered AUMID raise E_NOTIMPL
    return {};
  }
}

QStringList readTexts(const wun::UserNotification& notification) {
  QStringList texts;
  try {
    const auto binding = notification.Notification().Visual().GetBinding(
        wun::KnownNotificationBindings::ToastGeneric());
    if (!binding) return texts;
    for (const auto& element : binding.GetTextElements()) {
      const QString text = toQString(element.Text()).trimmed();
      if (!text.isEmpty()) texts << text;
    }
  } catch (const winrt::hresult_error& error) {
    qDebug().noquote() << "Notification" << notification.Id()
                       << "has no readable text:" << toQString(error.message());
  }
  return texts;
}

// The newest `count` toasts of an app from the toast history, newest first.
std::vector<ToastXml> readToastXml(const QString& aumid, int count) {
  std::vector<ToastXml> toasts;
  try {
    const auto history =
        wun::ToastNotificationManager::History().GetHistory(winrt::hstring(aumid.toStdWString()));
    for (const auto& toast : history) {
      if (static_cast<int>(toasts.size()) >= count) break;
      try {
        if (auto parsed = parseToastXml(toQString(toast.Content().GetXml())))
          toasts.push_back(std::move(*parsed));
      } catch (const winrt::hresult_error&) {
        // Windows keeps up to 100 entries per app but empties all but the newest 20
      }
    }
  } catch (const winrt::hresult_error& error) {
    qDebug().noquote() << "Failed to read the toast history of" << aumid << ":"
                       << toQString(error.message());
    return {};
  }
  return toasts;
}

QDateTime toDateTime(const winrt::Windows::Foundation::DateTime& time) {
  // 100 ns ticks since 1601-01-01
  const long long ticks = time.time_since_epoch().count();
  return QDateTime::fromMSecsSinceEpoch(ticks / 10000 - 11644473600000LL, QTimeZone::UTC);
}

class SystemNotificationSource final : public NotificationSource {
 public:
  ~SystemNotificationSource() override { stop(); }

  void start(NotificationSink* sink) override {
    if (thread_.joinable()) return;
    sink_ = sink;
    thread_ = std::thread([this] { run(); });
    subscribeWnf();
  }

  void stop() override {
    if (!thread_.joinable()) return;
    unsubscribeWnf();
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
    sink_ = nullptr;
  }

  void clearAll() override {
    enqueue([this] { clear(std::nullopt); });
  }
  void clearApp(const QString& aumid) override {
    enqueue([this, aumid] { clear(aumid); });
  }
  void refresh() override { scheduleRefresh(); }
  void remove(std::uint32_t id) override {
    enqueue([this, id] { removeNotification(id); });
  }
  void activate(const NotificationItem& item) override {
    enqueue([this, item] { activateItem(item); });
  }

 private:
  // WNF system thread. WnfSubscription keeps `this` alive for the call: stop() waits for it.
  void onWnf(const void* buffer, unsigned long bufferSize) {
    scheduleRefresh();
    // The buffer is a u32 count. The sink only posts to the GUI thread and does not throw.
    if (sink_ && buffer && bufferSize >= sizeof(std::uint32_t)) {
      std::uint32_t count = 0;
      std::memcpy(&count, buffer, sizeof(count));
      sink_->countChanged(count);
    }
  }

  bool trySubscribe(ULONGLONG state, const char* name) {
    const long status = wnf_.subscribe(
        state, [this](const void* buffer, unsigned long size) { onWnf(buffer, size); });
    if (status == 0) {
      qInfo().noquote() << "Notification service started (" << name << ")";
      return true;
    }
    qDebug().noquote() << name << "unavailable, status" << Qt::hex << static_cast<ULONG>(status);
    return false;
  }

  // Windows 11 total first, then the Windows 10 badge count; both missing leaves the count at 0.
  void subscribeWnf() {
    if (!wnf_.available()) {
      qWarning() << "WNF is not available, the notification count stays at 0";
      return;
    }
    if (trySubscribe(kWnfShellNotificationTotal, "WNF_SHEL_NOTIFICATION_TOTAL")) return;
    if (trySubscribe(kWnfShellNotifications, "WNF_SHEL_NOTIFICATIONS")) return;
    qWarning() << "Notification service failed to subscribe to WNF, the count stays at 0";
  }

  void unsubscribeWnf() { wnf_.unsubscribe(); }

  void run() {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
      // Already initialised on this thread; carry on.
    }
    try {
      listener_ = wum::UserNotificationListener::Current();
      const auto status = listener_.RequestAccessAsync().get();
      if (status != wum::UserNotificationListenerAccessStatus::Allowed) {
        qWarning().noquote() << "Notification access is turned off (status"
                             << static_cast<int>(status)
                             << "), the notification list will be empty";
      }
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Notification listener unavailable:" << toQString(error.message());
    } catch (const std::exception& error) {
      qWarning().noquote() << "Notification listener unavailable:" << error.what();
    }

    for (;;) {
      std::function<void()> task;
      bool refreshNow = false;
      {
        std::unique_lock lock(mutex_);
        for (;;) {
          if (stopping_) break;
          if (!tasks_.empty()) {
            task = std::move(tasks_.front());
            tasks_.pop_front();
            break;
          }
          if (refreshPending_ && std::chrono::steady_clock::now() >= refreshAt_) {
            refreshPending_ = false;
            refreshNow = true;
            break;
          }
          if (refreshPending_) {
            wake_.wait_until(lock, refreshAt_);
          } else {
            wake_.wait(lock);
          }
        }
        if (stopping_) break;
      }
      try {
        if (refreshNow) {
          readAndReport();
        } else {
          task();
        }
      } catch (const winrt::hresult_error& error) {
        qWarning().noquote() << "Notification task failed:" << toQString(error.message());
      } catch (const std::exception& error) {
        qWarning().noquote() << "Notification task failed:" << error.what();
      }
    }
    listener_ = nullptr;
  }

  void enqueue(std::function<void()> task) {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) return;
      tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
  }

  // Arms the debounced list read (a burst of changes is read once). Any thread.
  void scheduleRefresh() {
    {
      std::lock_guard lock(mutex_);
      if (stopping_ || refreshPending_) return;
      refreshPending_ = true;
      refreshAt_ = std::chrono::steady_clock::now() + kRefreshDelay;
    }
    wake_.notify_one();
  }

  void readAndReport() {
    std::optional<std::vector<NotificationItem>> items = readNotifications();
    items_ = items.value_or(std::vector<NotificationItem>{});
    if (sink_) sink_->itemsChanged(std::move(items));
  }

  // Newest first, or nullopt while notification access is turned off (_read_notifications).
  std::optional<std::vector<NotificationItem>> readNotifications() {
    if (!listener_) return std::nullopt;
    std::vector<wun::UserNotification> notifications;
    try {
      if (listener_.GetAccessStatus() != wum::UserNotificationListenerAccessStatus::Allowed)
        return std::nullopt;
      for (const auto& n : listener_.GetNotificationsAsync(wun::NotificationKinds::Toast).get())
        notifications.push_back(n);
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Error reading notifications:" << toQString(error.message());
      return std::vector<NotificationItem>{};
    }
    std::stable_sort(notifications.begin(), notifications.end(), [](const auto& a, const auto& b) {
      return a.CreationTime() > b.CreationTime();
    });
    // AppInfo is a slow cross-process call, and a notification's app never changes.
    std::map<std::uint32_t, AppInfoData> appInfo;
    std::vector<RawToast> raw;
    raw.reserve(notifications.size());
    for (const auto& n : notifications) {
      const std::uint32_t id = n.Id();
      const auto cached = appInfo_.find(id);
      const AppInfoData& info =
          appInfo.insert_or_assign(id, cached != appInfo_.end() ? cached->second : readAppInfo(n))
              .first->second;
      raw.push_back(
          {id, info.aumid, info.name, info.family, toDateTime(n.CreationTime()), readTexts(n)});
    }
    appInfo_ = std::move(appInfo);
    return assembleItems(raw, readToastXml, imageEnv);
  }

  void activateItem(const NotificationItem& item) {
    bool handled = activateToast(item.aumid, item.launch, item.activationType);
    if (!handled) handled = showApp(item.aumid);
    if (handled) removeNotification(item.id);
  }

  void removeNotification(std::uint32_t id) {
    if (!listener_) return;
    try {
      listener_.RemoveNotification(id);
    } catch (const winrt::hresult_error& error) {
      qDebug().noquote() << "Failed to remove notification" << id << ":"
                         << toQString(error.message());
    }
    scheduleRefresh();
  }

  // Port of _clear: removes every notification, or only those of `aumid`. Each app's history is
  // cleared by AUMID (ClearNotifications() needs package identity), then what is left goes
  // through the listener.
  void clear(const std::optional<QString>& aumid) {
    if (!listener_) return;
    std::set<QString> apps;
    for (const NotificationItem& item : items_) {
      if (!aumid || *aumid == item.aumid) apps.insert(item.aumid);
    }
    std::vector<std::uint32_t> leftovers;
    if (!aumid) {
      try {
        for (const auto& n : listener_.GetNotificationsAsync(wun::NotificationKinds::Toast).get()) {
          leftovers.push_back(n.Id());
          const QString id = readAppInfo(n).aumid;
          if (!id.isEmpty()) apps.insert(id);
        }
      } catch (const winrt::hresult_error& error) {
        qWarning().noquote() << "Error clearing notifications:" << toQString(error.message());
      }
    } else if (aumid->isEmpty()) {
      for (const NotificationItem& item : items_)
        if (item.aumid.isEmpty()) leftovers.push_back(item.id);
    }
    apps.erase(QString());
    for (const QString& app : apps) {
      try {
        wun::ToastNotificationManager::History().Clear(winrt::hstring(app.toStdWString()));
      } catch (const winrt::hresult_error& error) {
        qDebug().noquote() << "Failed to clear the notifications of" << app << ":"
                           << toQString(error.message());
      }
    }
    for (const std::uint32_t id : leftovers) {
      try {
        listener_.RemoveNotification(id);
      } catch (const winrt::hresult_error& error) {
        qDebug().noquote() << "Failed to remove notification" << id << ":"
                           << toQString(error.message());
      }
    }
    scheduleRefresh();
  }

  NotificationSink* sink_ = nullptr;  // set before the subscription, cleared after it is gone
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> tasks_;
  bool stopping_ = false;
  bool refreshPending_ = false;
  std::chrono::steady_clock::time_point refreshAt_;
  // Worker thread only: the last list read and the per-notification app info cache.
  std::vector<NotificationItem> items_;
  std::map<std::uint32_t, AppInfoData> appInfo_;
  wum::UserNotificationListener listener_{nullptr};
  // Last, so it is gone before anything its callback touches.
  WnfSubscription wnf_;
};

}  // namespace

std::unique_ptr<NotificationSource> createSystemNotificationSource() {
  return std::make_unique<SystemNotificationSource>();
}

}  // namespace wabar::notifications

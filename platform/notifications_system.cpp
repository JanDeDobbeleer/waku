// Windows notification count and clear-all for NotificationService.
//
// Threading: the count comes from a WNF state subscription; its callback runs on a system thread
// and only forwards the value to the sink. Clearing runs on one worker thread (MTA) that owns the
// UserNotificationListener; clearAll() enqueues a task and the thread sleeps on a condition
// variable otherwise. stop() unsubscribes WNF (which waits for a running callback), wakes the
// worker and joins it.
#include <Windows.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <QDebug>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "platform/notifications.h"

namespace yasb::platform {
namespace {

namespace wun = winrt::Windows::UI::Notifications;
namespace wum = winrt::Windows::UI::Notifications::Management;

// Undocumented WNF state names (docs/architecture.md allowlist).
constexpr ULONGLONG kWnfShellNotificationTotal = 0x0D83063EA3B8D035ull;  // Windows 11 toast total
constexpr ULONGLONG kWnfShellNotifications = 0x0D83063EA3BC1035ull;      // Windows 10 unread badge

using WnfCallback = LONG(NTAPI*)(ULONGLONG stateName, ULONG changeStamp, void* typeId,
                                 void* context, const void* buffer, ULONG bufferSize);
using RtlSubscribeFn = LONG(NTAPI*)(void** subscription, ULONGLONG stateName, ULONG changeStamp,
                                    WnfCallback callback, void* context, void* typeId,
                                    ULONG serializationGroup, ULONG unknown);
using RtlUnsubscribeFn = LONG(NTAPI*)(void* subscription);

QString toQString(const winrt::hstring& text) {
  return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
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
    {
      std::lock_guard lock(mutex_);
      if (stopping_) return;
      tasks_.push_back([this] { clear(); });
    }
    wake_.notify_one();
  }

 private:
  static LONG NTAPI onWnf(ULONGLONG, ULONG, void*, void* context, const void* buffer,
                          ULONG bufferSize) {
    auto* self = static_cast<SystemNotificationSource*>(context);
    // The buffer is a u32 count. The sink only posts to the GUI thread and does not throw.
    if (self && self->sink_ && buffer && bufferSize >= sizeof(std::uint32_t)) {
      std::uint32_t count = 0;
      std::memcpy(&count, buffer, sizeof(count));
      self->sink_->countChanged(count);
    }
    return 0;
  }

  bool trySubscribe(ULONGLONG state, const char* name) {
    void* subscription = nullptr;
    const LONG status =
        subscribe_(&subscription, state, 0, &SystemNotificationSource::onWnf, this, nullptr, 0, 0);
    if (status == 0) {
      subscription_ = subscription;
      qInfo().noquote() << "Notification service started (" << name << ")";
      return true;
    }
    qDebug().noquote() << name << "unavailable, status" << Qt::hex << static_cast<ULONG>(status);
    return false;
  }

  // Windows 11 total first, then the Windows 10 badge count; both missing leaves the count at 0.
  void subscribeWnf() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
      subscribe_ = reinterpret_cast<RtlSubscribeFn>(
          GetProcAddress(ntdll, "RtlSubscribeWnfStateChangeNotification"));
      unsubscribe_ = reinterpret_cast<RtlUnsubscribeFn>(
          GetProcAddress(ntdll, "RtlUnsubscribeWnfStateChangeNotification"));
    }
    if (!subscribe_ || !unsubscribe_) {
      qWarning() << "WNF is not available, the notification count stays at 0";
      return;
    }
    if (trySubscribe(kWnfShellNotificationTotal, "WNF_SHEL_NOTIFICATION_TOTAL")) return;
    if (trySubscribe(kWnfShellNotifications, "WNF_SHEL_NOTIFICATIONS")) return;
    qWarning() << "Notification service failed to subscribe to WNF, the count stays at 0";
  }

  void unsubscribeWnf() {
    if (!subscription_ || !unsubscribe_) return;
    unsubscribe_(subscription_);
    subscription_ = nullptr;
  }

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
                             << "), the notification list cannot be cleared";
      }
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Notification listener unavailable:" << toQString(error.message());
    } catch (const std::exception& error) {
      qWarning().noquote() << "Notification listener unavailable:" << error.what();
    }

    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_) break;
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      try {
        task();
      } catch (const winrt::hresult_error& error) {
        qWarning().noquote() << "Notification task failed:" << toQString(error.message());
      } catch (const std::exception& error) {
        qWarning().noquote() << "Notification task failed:" << error.what();
      }
    }
    listener_ = nullptr;
  }

  // Port of _clear(None): clear each app's history by AUMID (ClearNotifications() needs package
  // identity), then remove what is left through the listener.
  void clear() {
    if (!listener_) return;
    if (listener_.GetAccessStatus() != wum::UserNotificationListenerAccessStatus::Allowed) {
      qWarning() << "Notification access is turned off, nothing to clear";
      return;
    }
    std::set<winrt::hstring> aumids;
    for (const auto& n : listener_.GetNotificationsAsync(wun::NotificationKinds::Toast).get()) {
      try {
        const winrt::hstring id = n.AppInfo().AppUserModelId();
        if (!id.empty()) aumids.insert(id);
      } catch (const winrt::hresult_error&) {
        // senders without a registered AUMID raise E_NOTIMPL
      }
    }
    for (const auto& aumid : aumids) {
      try {
        wun::ToastNotificationManager::History().Clear(aumid);
      } catch (const winrt::hresult_error& error) {
        qDebug().noquote() << "Failed to clear the notifications of" << toQString(aumid) << ":"
                           << toQString(error.message());
      }
    }
    for (const auto& n : listener_.GetNotificationsAsync(wun::NotificationKinds::Toast).get()) {
      try {
        listener_.RemoveNotification(n.Id());
      } catch (const winrt::hresult_error& error) {
        qDebug().noquote() << "Failed to remove notification" << n.Id() << ":"
                           << toQString(error.message());
      }
    }
  }

  NotificationSink* sink_ = nullptr;  // set before the subscription, cleared after it is gone
  RtlSubscribeFn subscribe_ = nullptr;
  RtlUnsubscribeFn unsubscribe_ = nullptr;
  void* subscription_ = nullptr;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> tasks_;
  bool stopping_ = false;
  wum::UserNotificationListener listener_{nullptr};
};

}  // namespace

std::unique_ptr<NotificationSource> createSystemNotificationSource() {
  return std::make_unique<SystemNotificationSource>();
}

}  // namespace yasb::platform

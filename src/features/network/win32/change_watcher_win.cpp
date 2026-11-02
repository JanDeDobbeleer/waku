#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <wlanapi.h>

#include <QPointer>
#include <memory>

#include "features/network/change_watcher.h"
#include "kernel/qt/gui_post.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/winrt_apartment.h"

namespace waku::network {
namespace {

namespace wnc = winrt::Windows::Networking::Connectivity;

constexpr DWORD kClientVersion = 2;

// The WLAN notifications that change what the bar shows: a connection came, went or roamed, the
// signal quality moved, an adapter appeared or vanished, the radio switched. Scan results,
// profile edits and the like are left out so a background scan does not wake the poll.
bool isStateChange(const WLAN_NOTIFICATION_DATA& data) {
  if (data.NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM) {
    switch (data.NotificationCode) {
      case wlan_notification_acm_connection_complete:
      case wlan_notification_acm_connection_attempt_fail:
      case wlan_notification_acm_disconnected:
      case wlan_notification_acm_interface_arrival:
      case wlan_notification_acm_interface_removal:
      case wlan_notification_acm_power_setting_change:
        return true;
      default:
        return false;
    }
  }
  if (data.NotificationSource == WLAN_NOTIFICATION_SOURCE_MSM) {
    switch (data.NotificationCode) {
      case wlan_notification_msm_connected:
      case wlan_notification_msm_disconnected:
      case wlan_notification_msm_roaming_end:
      case wlan_notification_msm_radio_state_change:
      case wlan_notification_msm_signal_quality_change:
        return true;
      default:
        return false;
    }
  }
  return false;
}

// Windows.Networking.Connectivity.NetworkStatusChanged (connections and addresses; its handler runs
// on a WinRT pool thread) and a wlanapi notification registration (signal quality, roaming; its
// callback runs on a wlanapi thread). Both callbacks only post `changed` to the GUI thread, through
// a guard that is empty once this object is gone. Either registration may fail, then the other
// still works; with neither, the service polls on its timer alone. Created, and destroyed (the
// registrations are removed first), on the GUI thread.
class SystemChangeWatcher final : public NetworkChangeWatcher {
 public:
  SystemChangeWatcher() {
    QPointer<SystemChangeWatcher> guard(this);
    try {
      kernel::ensureWinrtApartment();
      statusToken_ = wnc::NetworkInformation::NetworkStatusChanged(
          [guard](const winrt::Windows::Foundation::IInspectable&) { post(guard); });
      statusRegistered_ = true;
    } catch (...) {
    }
    DWORD version = 0;
    HANDLE handle = nullptr;
    if (WlanOpenHandle(kClientVersion, nullptr, &version, &handle) == ERROR_SUCCESS) {
      context_ = std::make_unique<Context>();
      context_->guard = guard;
      if (WlanRegisterNotification(
              handle, WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM, TRUE,
              &SystemChangeWatcher::onWlan, context_.get(), nullptr, nullptr) == ERROR_SUCCESS) {
        wlan_ = handle;
      } else {
        WlanCloseHandle(handle, nullptr);
        context_.reset();
      }
    }
  }

  ~SystemChangeWatcher() override {
    if (statusRegistered_) {
      try {
        wnc::NetworkInformation::NetworkStatusChanged(statusToken_);
      } catch (...) {
      }
    }
    if (wlan_) {
      WlanRegisterNotification(wlan_, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr,
                               nullptr, nullptr);
      WlanCloseHandle(wlan_, nullptr);
    }
  }

  bool registered() const { return statusRegistered_ || wlan_ != nullptr; }

 private:
  struct Context {
    QPointer<SystemChangeWatcher> guard;
  };

  static void post(const QPointer<SystemChangeWatcher>& guard) {
    kernel::postToGui(guard, [guard] { emit guard->changed(); });
  }

  static void WINAPI onWlan(PWLAN_NOTIFICATION_DATA data, PVOID context) {
    if (!data || !context || !isStateChange(*data)) return;
    post(static_cast<Context*>(context)->guard);
  }

  winrt::event_token statusToken_{};
  bool statusRegistered_ = false;
  HANDLE wlan_ = nullptr;
  std::unique_ptr<Context> context_;
};

}  // namespace

std::unique_ptr<NetworkChangeWatcher> createSystemChangeWatcher() {
  if (!kernel::liveDesktop()) return nullptr;
  auto watcher = std::make_unique<SystemChangeWatcher>();
  if (!watcher->registered()) return nullptr;
  return watcher;
}

}  // namespace waku::network

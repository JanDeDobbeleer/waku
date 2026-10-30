#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <atomic>
#include <optional>

#include "features/network/radio.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/winrt_apartment.h"

namespace wabar::network {
namespace {

namespace radios = winrt::Windows::Devices::Radios;

struct RadioState {
  bool known = false;
  bool on = false;
};

// The first Wi-Fi radio, or nothing. Blocks on WinRT calls: pool threads only.
std::optional<radios::Radio> findWifiRadio() {
  const auto all = radios::Radio::GetRadiosAsync().get();
  for (const radios::Radio& radio : all) {
    if (radio.Kind() == radios::RadioKind::WiFi) return radio;
  }
  return std::nullopt;
}

RadioState readState() {
  try {
    if (const auto radio = findWifiRadio()) return {true, radio->State() == radios::RadioState::On};
  } catch (...) {
  }
  return {};
}

QString accessText(radios::RadioAccessStatus status) {
  switch (status) {
    case radios::RadioAccessStatus::DeniedByUser:
      return QStringLiteral("Access to the Wi-Fi radio was denied by the user");
    case radios::RadioAccessStatus::DeniedBySystem:
      return QStringLiteral("Access to the Wi-Fi radio was denied by the system");
    default:
      return QStringLiteral("Windows did not allow the Wi-Fi radio change");
  }
}

// Windows.Devices.Radios, every call on a QThreadPool thread. A query is dropped while another one
// runs; switches are serialised by the Network service. Results return as queued signals through
// qApp, and are dropped when this object is gone. Nothing to join: a task ends with its call.
class SystemWifiRadio final : public WifiRadioBackend {
 public:
  void query() override {
    if (querying_.exchange(true)) return;
    QPointer<SystemWifiRadio> guard(this);
    QThreadPool::globalInstance()->start([guard] {
      kernel::ensureWinrtApartment();
      const RadioState state = readState();
      post(guard, [guard, state] {
        guard->querying_ = false;
        emit guard->stateRead(state.known, state.on);
      });
    });
  }

  void setEnabled(bool enabled) override {
    QPointer<SystemWifiRadio> guard(this);
    QThreadPool::globalInstance()->start([guard, enabled] {
      kernel::ensureWinrtApartment();
      QString error;
      try {
        const auto access = radios::Radio::RequestAccessAsync().get();
        if (access != radios::RadioAccessStatus::Allowed) {
          error = accessText(access);
        } else if (const auto radio = findWifiRadio()) {
          const auto result =
              radio->SetStateAsync(enabled ? radios::RadioState::On : radios::RadioState::Off)
                  .get();
          if (result != radios::RadioAccessStatus::Allowed) error = accessText(result);
        } else {
          error = QStringLiteral("There is no Wi-Fi radio");
        }
      } catch (const winrt::hresult_error& e) {
        error = QString::fromStdWString(std::wstring(e.message()));
        if (error.isEmpty()) error = QStringLiteral("The Wi-Fi radio could not be switched");
      } catch (...) {
        error = QStringLiteral("The Wi-Fi radio could not be switched");
      }
      const RadioState state = readState();
      post(guard, [guard, error, state] {
        emit guard->switchFinished(error.isEmpty(), error);
        emit guard->stateRead(state.known, state.on);
      });
    });
  }

 private:
  template <class Fn>
  static void post(const QPointer<SystemWifiRadio>& guard, Fn fn) {
    if (!qApp) return;
    QMetaObject::invokeMethod(
        qApp,
        [guard, fn = std::move(fn)]() mutable {
          if (guard) fn();
        },
        Qt::QueuedConnection);
  }

  std::atomic<bool> querying_{false};
};

}  // namespace

std::unique_ptr<WifiRadioBackend> createSystemWifiRadio() {
  if (!kernel::liveDesktop()) return nullptr;
  return std::make_unique<SystemWifiRadio>();
}

}  // namespace wabar::network

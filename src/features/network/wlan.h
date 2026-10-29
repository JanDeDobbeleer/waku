#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>
#include <optional>

namespace wabar::network {

// Port of wifi_managers.WifiState (an IntFlag).
enum WifiStateFlag : int {
  kWifiConnected = 1,
  kWifiSecured = 2,
  kWifiUnsecured = 4,
};

// Port of wifi_managers.NetworkInfo (popup data).
struct WifiNetwork {
  QString ssid;
  int quality = 0;
  QString icon;
  int state = kWifiUnsecured;
  int authAlg = 0;
  bool profileExists = false;
  bool autoConnect = false;

  bool operator==(const WifiNetwork&) const = default;
};

enum class WifiScanStatus { Success, AccessDenied, PowerStateInvalid, ServiceUnavailable, Error };

// Windows.Devices.WiFi.WiFiConnectionStatus values the menu distinguishes.
enum class WifiConnectStatus {
  Success,
  UnspecifiedFailure,
  AccessRevoked,
  InvalidCredential,
  NetworkNotAvailable,
  Timeout,
  UnsupportedAuthenticationProtocol,
};

// Asynchronous Wi-Fi operations behind the Wi-Fi menu. Every method returns at once; the work runs
// on a backend thread and the results arrive as queued signals on the caller's thread. Tests
// substitute a fake; the real backend (createWlanBackend) talks to wlanapi and
// Windows.Devices.WiFi and must never be driven from tests.
class WifiBackend : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  // Scan results are only delivered while updates are enabled (menu open).
  virtual void setUpdatesEnabled(bool enabled) = 0;
  // Starts a scan; scanCompleted follows (immediately on failure, on the WLAN notification else).
  virtual void scan() = 0;
  // Queries the connected network (currentConnection).
  virtual void queryCurrent() = 0;
  virtual void connectTo(const WifiNetwork& network, const QString& ssid, const QString& password,
                         bool hidden) = 0;
  virtual void disconnectCurrent() = 0;
  virtual void forget(const QString& profileName) = 0;
  virtual void setAutoConnect(const QString& ssid, bool autoConnect) = 0;

 signals:
  void scanCompleted(int status, const QVector<wabar::network::WifiNetwork>& networks);
  void currentConnection(bool connected, const wabar::network::WifiNetwork& network);
  void connectFinished(int status, const QString& ssid, const wabar::network::WifiNetwork& network);
  void connectFailed(const QString& message);
  void disconnected(const QString& profileName);
};

std::unique_ptr<WifiBackend> createWlanBackend();

// Synchronous query used by the polling worker for `get_exact_wifi_strength` (signal quality 0-100
// of the connected network). Returns nullopt when unavailable.
std::optional<int> currentSignalQuality();

}  // namespace wabar::network

Q_DECLARE_METATYPE(wabar::network::WifiNetwork)
Q_DECLARE_METATYPE(QVector<wabar::network::WifiNetwork>)

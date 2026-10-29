#pragma once

#include <QtQml/qqmlregistration.h>

#include <QString>
#include <QVariantList>
#include <memory>
#include <optional>
#include <vector>

#include "features/system_stats/qml/stat_source.h"
#include "platform/connectivity.h"
#include "platform/wlan.h"

namespace wabar::services {

namespace platform = ::yasb::platform;

// One poll of the network state: what the Wi-Fi widget shows.
struct NetworkStatus {
  // "wifi", "ethernet" or "none".
  QString kind = QStringLiteral("none");
  QString ssid;
  // Signal quality 0-100 of the Wi-Fi connection; 0 when not on Wi-Fi.
  int signalPercent = 0;
  QString ipAddress;
  // Empty when no VPN is connected.
  QString vpnName;

  bool operator==(const NetworkStatus&) const = default;
};

// Where the Network service gets its data. Tests substitute a fake; the real source (Windows
// connectivity profiles and wlanapi) must not be driven from tests.
class NetworkSource {
 public:
  virtual ~NetworkSource() = default;
  // One blocking poll. Runs on a QThreadPool thread, so it must not touch GUI objects.
  virtual NetworkStatus sample() = 0;
  // The Wi-Fi scan and connect backend. Called on the GUI thread the first time a scan or a
  // connect is requested; null when there is none.
  virtual std::unique_ptr<platform::WifiBackend> createWifiBackend() = 0;
};

// Maps one connectivity sample and the exact signal quality onto the status. Ethernet wins over
// Wi-Fi; the "Disconnected" and "Error" names the backend reports mean no connection.
NetworkStatus statusFromSample(const platform::ConnectivitySample& sample,
                               const std::optional<int>& exactQuality);

// The real source; null on the offscreen platform.
std::shared_ptr<NetworkSource> createSystemNetworkSource();

// Wi-Fi, Ethernet and VPN state as a QML singleton, shared by every Wifi widget. Polls the source
// every `interval` ms (inherited from StatSource, which runs the blocking sample on a thread-pool
// thread and applies it here on the GUI thread). `available` is false when there is no source
// (offscreen); `ready` turns true after the first sample. `networks` lists the nearby networks of
// the last scan as maps (ssid, signal, secured, connected, known), connected first, then by
// signal; it is filled by `refresh()` and stays empty until then. The Wi-Fi backend (and its
// thread) is created by the first refresh or connect and destroyed with this object, which stops
// it. GUI thread only.
class Network : public system_stats::StatSource {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(QString kind READ kind NOTIFY updated FINAL)
  Q_PROPERTY(QString ssid READ ssid NOTIFY updated FINAL)
  Q_PROPERTY(int signalPercent READ signalPercent NOTIFY updated FINAL)
  Q_PROPERTY(QString ipAddress READ ipAddress NOTIFY updated FINAL)
  Q_PROPERTY(bool vpnActive READ vpnActive NOTIFY updated FINAL)
  Q_PROPERTY(QString vpnName READ vpnName NOTIFY updated FINAL)
  Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged FINAL)
  Q_PROPERTY(bool scanning READ scanning NOTIFY networksChanged FINAL)

 public:
  explicit Network(QObject* parent = nullptr);
  // Test seam: a fake source (null for none). Polling starts at componentComplete().
  Network(std::shared_ptr<NetworkSource> source, QObject* parent);
  ~Network() override;

  bool available() const { return source_ != nullptr; }
  QString kind() const { return status_.kind; }
  QString ssid() const { return status_.ssid; }
  int signalPercent() const { return status_.signalPercent; }
  QString ipAddress() const { return status_.ipAddress; }
  bool vpnActive() const { return !status_.vpnName.isEmpty(); }
  QString vpnName() const { return status_.vpnName; }
  QVariantList networks() const { return networks_; }
  bool scanning() const { return scanning_; }

  // Starts a scan; `networks` follows.
  Q_INVOKABLE void refresh();
  // Connects to a network of the last scan that has a saved profile; others (they need a password)
  // are ignored. Returns whether a connect was started.
  Q_INVOKABLE bool connectTo(const QString& ssid);

 signals:
  void networksChanged();

 protected:
  void sample() override;

 private:
  platform::WifiBackend* backend();
  void onScanCompleted(int status, const QVector<platform::WifiNetwork>& networks);

  std::shared_ptr<NetworkSource> source_;
  std::unique_ptr<platform::WifiBackend> backend_;
  NetworkStatus status_;
  std::vector<platform::WifiNetwork> scanned_;
  QVariantList networks_;
  bool scanning_ = false;
  bool connecting_ = false;
};

}  // namespace wabar::services

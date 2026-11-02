#pragma once

#include <QtQml/qqmlregistration.h>

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <optional>
#include <vector>

#include "features/network/connectivity.h"
#include "features/network/radio.h"
#include "features/network/throughput.h"
#include "features/network/vpn.h"
#include "features/network/wlan.h"
#include "kernel/qml/stat_source.h"

namespace waku::network {

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
  virtual std::unique_ptr<WifiBackend> createWifiBackend() = 0;
  // The interface counter reader, called on the GUI thread when the throughput properties are first
  // observed; null when there is none (the default).
  virtual std::unique_ptr<ThroughputSource> createThroughputSource() { return nullptr; }
  // The Wi-Fi radio switch, called on the GUI thread at the first sample; null when there is none.
  virtual std::unique_ptr<WifiRadioBackend> createWifiRadio() { return nullptr; }
  // The VPN (RAS) backend, called on the GUI thread at the first sample; null when there is none.
  virtual std::unique_ptr<VpnBackend> createVpnBackend() { return nullptr; }
};

// Maps one connectivity sample and the exact signal quality onto the status. Ethernet wins over
// Wi-Fi; the "Disconnected" and "Error" names the backend reports mean no connection.
NetworkStatus statusFromSample(const ConnectivitySample& sample,
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
//
// Throughput: `rxBytesPerSecond`, `txBytesPerSecond` (sum of the non-loopback interfaces that are
// up), `rxBytesTotal`, `txBytesTotal` (bytes counted since the service started sampling) and
// `adapters` (maps: name, isUp, rxBytesPerSecond, txBytesPerSecond) refresh once per second, and
// only while something is connected to `throughputChanged` (a QML binding to one of them is
// enough). Unobserved, nothing is read and the rates are zero. They are zero too without a source.
//
// Wi-Fi radio: `wifiEnabled` is the state Windows reports (read at each poll, never assumed).
// `setWifiEnabled(bool)` asks Windows to switch it; a refusal (no permission, no radio) shows in
// `wifiSwitchError` and leaves `wifiEnabled` alone. `wifiAvailable` is false when no Wi-Fi radio
// could be read.
//
// VPN: `vpnConnections` lists the configured Windows VPN entries (maps: name, connected);
// `connectVpn` / `disconnectVpn` dial or hang up one of them, and a failure shows in `vpnError`.
class Network : public kernel::StatSource {
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
  Q_PROPERTY(double rxBytesPerSecond READ rxBytesPerSecond NOTIFY throughputChanged FINAL)
  Q_PROPERTY(double txBytesPerSecond READ txBytesPerSecond NOTIFY throughputChanged FINAL)
  Q_PROPERTY(double rxBytesTotal READ rxBytesTotal NOTIFY throughputChanged FINAL)
  Q_PROPERTY(double txBytesTotal READ txBytesTotal NOTIFY throughputChanged FINAL)
  Q_PROPERTY(QVariantList adapters READ adapters NOTIFY throughputChanged FINAL)
  Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY wifiChanged FINAL)
  Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY wifiChanged FINAL)
  Q_PROPERTY(bool wifiSwitching READ wifiSwitching NOTIFY wifiChanged FINAL)
  Q_PROPERTY(QString wifiSwitchError READ wifiSwitchError NOTIFY wifiChanged FINAL)
  Q_PROPERTY(QVariantList vpnConnections READ vpnConnections NOTIFY vpnChanged FINAL)
  Q_PROPERTY(bool vpnBusy READ vpnBusy NOTIFY vpnChanged FINAL)
  Q_PROPERTY(QString vpnError READ vpnError NOTIFY vpnChanged FINAL)

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
  double rxBytesPerSecond() const { return tracker_.rxBytesPerSecond(); }
  double txBytesPerSecond() const { return tracker_.txBytesPerSecond(); }
  double rxBytesTotal() const { return tracker_.rxBytesTotal(); }
  double txBytesTotal() const { return tracker_.txBytesTotal(); }
  QVariantList adapters() const;
  bool wifiAvailable() const { return wifiKnown_; }
  bool wifiEnabled() const { return wifiOn_; }
  bool wifiSwitching() const { return wifiSwitching_; }
  QString wifiSwitchError() const { return wifiSwitchError_; }
  QVariantList vpnConnections() const { return vpnConnections_; }
  bool vpnBusy() const { return vpnBusy_; }
  QString vpnError() const { return vpnError_; }

  // Test seam: the period of the throughput sampler (default 1000 ms).
  void setThroughputInterval(int ms);
  // Whether the throughput sampler runs; false means no counter read can come (tests).
  bool throughputSampling() const { return throughputTimer_.isActive(); }

  // Starts a scan; `networks` follows.
  Q_INVOKABLE void refresh();
  // Connects to a network of the last scan that has a saved profile; others (they need a password)
  // are ignored. Returns whether a connect was started.
  Q_INVOKABLE bool connectTo(const QString& ssid);
  // Asks Windows to switch the Wi-Fi radio. Returns whether a request was started; otherwise
  // `wifiSwitchError` says why.
  Q_INVOKABLE bool setWifiEnabled(bool enabled);
  // Dials / hangs up a configured VPN entry. Return whether a request was started.
  Q_INVOKABLE bool connectVpn(const QString& name);
  Q_INVOKABLE bool disconnectVpn(const QString& name);

 signals:
  void networksChanged();
  void throughputChanged();
  void wifiChanged();
  void vpnChanged();

 protected:
  void sample() override;
  void connectNotify(const QMetaMethod& signal) override;
  void disconnectNotify(const QMetaMethod& signal) override;

 private:
  WifiBackend* backend();
  WifiRadioBackend* radio();
  VpnBackend* vpn();
  bool throughputObserved() const;
  void updateThroughputSampling();
  void sampleThroughput();
  bool startVpnOperation(const QString& name, bool dial);
  void onScanCompleted(int status, const QVector<WifiNetwork>& networks);

  std::shared_ptr<NetworkSource> source_;
  std::unique_ptr<WifiBackend> backend_;
  NetworkStatus status_;
  std::vector<WifiNetwork> scanned_;
  QVariantList networks_;
  bool scanning_ = false;
  bool connecting_ = false;

  std::unique_ptr<ThroughputSource> throughputSource_;
  ThroughputTracker tracker_;
  QTimer throughputTimer_;
  QElapsedTimer throughputClock_;

  std::unique_ptr<WifiRadioBackend> radio_;
  bool wifiKnown_ = false;
  bool wifiOn_ = false;
  bool wifiSwitching_ = false;
  QString wifiSwitchError_;

  std::unique_ptr<VpnBackend> vpn_;
  QVariantList vpnConnections_;
  bool vpnBusy_ = false;
  QString vpnError_;
};

}  // namespace waku::network

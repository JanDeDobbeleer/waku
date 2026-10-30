#include "features/network/qml/network_service.h"

#include <QMetaMethod>
#include <algorithm>

#include "kernel/qt/live_desktop.h"

namespace wabar::network {

NetworkStatus statusFromSample(const ConnectivitySample& sample,
                               const std::optional<int>& exactQuality) {
  const WifiInfo& info = sample.info;
  NetworkStatus status;
  status.vpnName = QString::fromStdString(info.vpnName);
  if (sample.ipAddress) status.ipAddress = QString::fromStdString(*sample.ipAddress);
  if (info.ethernet) {
    status.kind = QStringLiteral("ethernet");
  } else if (!info.name.empty() && info.name != "Disconnected" && info.name != "Error") {
    status.kind = QStringLiteral("wifi");
    status.ssid = QString::fromStdString(info.name);
    // Windows reports signal bars; four quarters are what the widget draws.
    status.signalPercent = exactQuality && *exactQuality >= 0 ? std::clamp(*exactQuality, 0, 100)
                                                              : std::clamp(info.bars, 0, 4) * 25;
  }
  return status;
}

namespace {

class SystemNetworkSource final : public NetworkSource {
 public:
  NetworkStatus sample() override {
    const ConnectivitySample sample = sampleConnectivity();
    return statusFromSample(sample, currentSignalQuality());
  }
  std::unique_ptr<WifiBackend> createWifiBackend() override { return createWlanBackend(); }
  std::unique_ptr<ThroughputSource> createThroughputSource() override {
    return createSystemThroughputSource();
  }
  std::unique_ptr<WifiRadioBackend> createWifiRadio() override { return createSystemWifiRadio(); }
  std::unique_ptr<VpnBackend> createVpnBackend() override { return createSystemVpnBackend(); }
};

QVariantMap networkMap(const WifiNetwork& network) {
  return {{QStringLiteral("ssid"), network.ssid},
          {QStringLiteral("signal"), network.quality},
          {QStringLiteral("secured"), (network.state & kWifiSecured) != 0},
          {QStringLiteral("connected"), (network.state & kWifiConnected) != 0},
          {QStringLiteral("known"), network.profileExists}};
}

}  // namespace

std::shared_ptr<NetworkSource> createSystemNetworkSource() {
  if (!kernel::liveDesktop()) return nullptr;
  return std::make_shared<SystemNetworkSource>();
}

Network::Network(QObject* parent) : Network(createSystemNetworkSource(), parent) {
  if (source_) componentComplete();  // a singleton gets no QQmlParserStatus callbacks
}

Network::Network(std::shared_ptr<NetworkSource> source, QObject* parent)
    : kernel::StatSource(parent), source_(std::move(source)) {
  throughputTimer_.setInterval(1000);
  connect(&throughputTimer_, &QTimer::timeout, this, &Network::sampleThroughput);
}

Network::~Network() = default;

void Network::sample() {
  if (!source_) return;
  sampleAsync(source_, [this](NetworkStatus status) { status_ = std::move(status); });
  if (WifiRadioBackend* switchBackend = radio()) switchBackend->query();
  if (VpnBackend* vpnBackend = vpn()) vpnBackend->refresh();
}

QVariantList Network::adapters() const {
  QVariantList list;
  for (const AdapterRate& adapter : tracker_.adapters()) {
    list << QVariantMap{{QStringLiteral("name"), adapter.name},
                        {QStringLiteral("isUp"), adapter.isUp},
                        {QStringLiteral("rxBytesPerSecond"), adapter.rxBytesPerSecond},
                        {QStringLiteral("txBytesPerSecond"), adapter.txBytesPerSecond}};
  }
  return list;
}

void Network::setThroughputInterval(int ms) { throughputTimer_.setInterval(std::max(ms, 1)); }

bool Network::throughputObserved() const {
  return source_ && isSignalConnected(QMetaMethod::fromSignal(&Network::throughputChanged));
}

void Network::connectNotify(const QMetaMethod& signal) {
  if (signal == QMetaMethod::fromSignal(&Network::throughputChanged)) updateThroughputSampling();
}

void Network::disconnectNotify(const QMetaMethod& signal) {
  if (signal == QMetaMethod::fromSignal(&Network::throughputChanged)) updateThroughputSampling();
}

namespace {

// Exceptions must not cross the Qt boundary (a timer slot, a connect notification).
std::vector<InterfaceCounters> readCounters(ThroughputSource& source) {
  try {
    return source.read();
  } catch (...) {
    return {};
  }
}

}  // namespace

// Samples only while the throughput properties are observed: the timer runs from the first
// connection to `throughputChanged` to the last disconnection, so an unused bar reads nothing.
void Network::updateThroughputSampling() {
  const bool observed = throughputObserved();
  if (observed == throughputTimer_.isActive()) return;
  if (!observed) {
    throughputTimer_.stop();
    tracker_.resetBaseline();
    tracker_.clearRates();
    return;
  }
  if (!throughputSource_) throughputSource_ = source_->createThroughputSource();
  if (!throughputSource_) return;
  tracker_.resetBaseline();
  tracker_.update(readCounters(*throughputSource_), 0);  // the baseline; rates follow at the tick
  throughputClock_.start();
  throughputTimer_.start();
}

void Network::sampleThroughput() {
  if (!throughputSource_ || !throughputObserved()) {
    updateThroughputSampling();
    return;
  }
  const double elapsed = static_cast<double>(throughputClock_.restart()) / 1000.0;
  tracker_.update(readCounters(*throughputSource_), elapsed);
  emit throughputChanged();
}

WifiRadioBackend* Network::radio() {
  if (!radio_ && source_) {
    radio_ = source_->createWifiRadio();
    if (radio_) {
      connect(radio_.get(), &WifiRadioBackend::stateRead, this, [this](bool known, bool on) {
        if (wifiKnown_ == known && wifiOn_ == (known && on)) return;
        wifiKnown_ = known;
        wifiOn_ = known && on;
        emit wifiChanged();
      });
      connect(radio_.get(), &WifiRadioBackend::switchFinished, this,
              [this](bool ok, const QString& error) {
                wifiSwitching_ = false;
                wifiSwitchError_ =
                    ok ? QString()
                       : (error.isEmpty() ? QStringLiteral("The Wi-Fi radio could not be switched")
                                          : error);
                emit wifiChanged();
              });
    }
  }
  return radio_.get();
}

bool Network::setWifiEnabled(bool enabled) {
  if (wifiSwitching_) return false;
  WifiRadioBackend* switchBackend = radio();
  if (!switchBackend) {
    wifiSwitchError_ = QStringLiteral("No Wi-Fi radio control is available");
    emit wifiChanged();
    return false;
  }
  wifiSwitching_ = true;
  wifiSwitchError_.clear();
  emit wifiChanged();
  switchBackend->setEnabled(enabled);
  return true;
}

VpnBackend* Network::vpn() {
  if (!vpn_ && source_) {
    vpn_ = source_->createVpnBackend();
    if (vpn_) {
      connect(vpn_.get(), &VpnBackend::entriesRead, this, [this](const QVector<VpnEntry>& entries) {
        QVariantList list;
        for (const VpnEntry& entry : entries) {
          list << QVariantMap{{QStringLiteral("name"), entry.name},
                              {QStringLiteral("connected"), entry.connected}};
        }
        if (list == vpnConnections_) return;
        vpnConnections_ = std::move(list);
        emit vpnChanged();
      });
      connect(vpn_.get(), &VpnBackend::operationFinished, this,
              [this](const QString&, bool ok, const QString& error) {
                vpnBusy_ = false;
                vpnError_ =
                    ok ? QString()
                       : (error.isEmpty() ? QStringLiteral("The VPN request failed") : error);
                emit vpnChanged();
                vpn_->refresh();
                sample();
              });
    }
  }
  return vpn_.get();
}

bool Network::startVpnOperation(const QString& name, bool dial) {
  if (vpnBusy_) return false;
  VpnBackend* vpnBackend = vpn();
  if (!vpnBackend) {
    vpnError_ = QStringLiteral("No VPN control is available");
    emit vpnChanged();
    return false;
  }
  const auto it = std::find_if(vpnConnections_.begin(), vpnConnections_.end(), [&](const auto& v) {
    return v.toMap().value(QStringLiteral("name")).toString() == name;
  });
  if (it == vpnConnections_.end()) {
    vpnError_ = QStringLiteral("Unknown VPN: ") + name;
    emit vpnChanged();
    return false;
  }
  if (it->toMap().value(QStringLiteral("connected")).toBool() == dial) return false;
  vpnBusy_ = true;
  vpnError_.clear();
  emit vpnChanged();
  if (dial) {
    vpnBackend->connectEntry(name);
  } else {
    vpnBackend->disconnectEntry(name);
  }
  return true;
}

bool Network::connectVpn(const QString& name) { return startVpnOperation(name, true); }

bool Network::disconnectVpn(const QString& name) { return startVpnOperation(name, false); }

WifiBackend* Network::backend() {
  if (!backend_ && source_) {
    backend_ = source_->createWifiBackend();
    if (backend_) {
      connect(backend_.get(), &WifiBackend::scanCompleted, this, &Network::onScanCompleted);
      connect(backend_.get(), &WifiBackend::connectFinished, this, [this] {
        connecting_ = false;
        refresh();
        sample();
      });
      connect(backend_.get(), &WifiBackend::connectFailed, this, [this] { connecting_ = false; });
    }
  }
  return backend_.get();
}

void Network::refresh() {
  WifiBackend* wifi = backend();
  if (!wifi) return;
  scanning_ = true;
  emit networksChanged();
  wifi->setUpdatesEnabled(true);
  wifi->scan();
}

void Network::onScanCompleted(int status, const QVector<WifiNetwork>& networks) {
  scanning_ = false;
  scanned_.clear();
  if (static_cast<WifiScanStatus>(status) == WifiScanStatus::Success) {
    for (const WifiNetwork& network : networks) {
      if (network.ssid.isEmpty()) continue;
      auto same = std::find_if(scanned_.begin(), scanned_.end(),
                               [&](const auto& n) { return n.ssid == network.ssid; });
      if (same == scanned_.end()) {
        scanned_.push_back(network);
      } else if (network.quality > same->quality) {
        *same = network;
      }
    }
    std::stable_sort(scanned_.begin(), scanned_.end(), [](const auto& a, const auto& b) {
      const bool ca = (a.state & kWifiConnected) != 0;
      const bool cb = (b.state & kWifiConnected) != 0;
      if (ca != cb) return ca;
      return a.quality > b.quality;
    });
  }
  networks_.clear();
  for (const WifiNetwork& network : scanned_) networks_ << networkMap(network);
  emit networksChanged();
}

bool Network::connectTo(const QString& ssid) {
  if (connecting_) return false;
  const auto it =
      std::find_if(scanned_.begin(), scanned_.end(), [&](const auto& n) { return n.ssid == ssid; });
  if (it == scanned_.end() || !it->profileExists) return false;
  WifiBackend* wifi = backend();
  if (!wifi) return false;
  connecting_ = true;
  wifi->connectTo(*it, ssid, QString(), false);
  return true;
}

}  // namespace wabar::network

#include "features/network/qml/network_service.h"

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
    : kernel::StatSource(parent), source_(std::move(source)) {}

Network::~Network() = default;

void Network::sample() {
  if (!source_) return;
  sampleAsync(source_, [this](NetworkStatus status) { status_ = std::move(status); });
}

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

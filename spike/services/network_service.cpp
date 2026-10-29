#include "spike/services/network_service.h"

#include <QGuiApplication>
#include <algorithm>

namespace wabar::services {

NetworkStatus statusFromSample(const platform::ConnectivitySample& sample,
                               const std::optional<int>& exactQuality) {
  const platform::WifiInfo& info = sample.info;
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
    const platform::ConnectivitySample sample = platform::sampleConnectivity();
    return statusFromSample(sample, platform::currentSignalQuality());
  }
  std::unique_ptr<platform::WifiBackend> createWifiBackend() override {
    return platform::createWlanBackend();
  }
};

QVariantMap networkMap(const platform::WifiNetwork& network) {
  return {{QStringLiteral("ssid"), network.ssid},
          {QStringLiteral("signal"), network.quality},
          {QStringLiteral("secured"), (network.state & platform::kWifiSecured) != 0},
          {QStringLiteral("connected"), (network.state & platform::kWifiConnected) != 0},
          {QStringLiteral("known"), network.profileExists}};
}

}  // namespace

std::shared_ptr<NetworkSource> createSystemNetworkSource() {
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return nullptr;
  return std::make_shared<SystemNetworkSource>();
}

Network::Network(QObject* parent) : Network(createSystemNetworkSource(), parent) {
  if (source_) componentComplete();  // a singleton gets no QQmlParserStatus callbacks
}

Network::Network(std::shared_ptr<NetworkSource> source, QObject* parent)
    : StatSource(parent), source_(std::move(source)) {}

Network::~Network() = default;

void Network::sample() {
  if (!source_) return;
  sampleAsync(source_, [this](NetworkStatus status) { status_ = std::move(status); });
}

platform::WifiBackend* Network::backend() {
  if (!backend_ && source_) {
    backend_ = source_->createWifiBackend();
    if (backend_) {
      connect(backend_.get(), &platform::WifiBackend::scanCompleted, this,
              &Network::onScanCompleted);
      connect(backend_.get(), &platform::WifiBackend::connectFinished, this, [this] {
        connecting_ = false;
        refresh();
        sample();
      });
      connect(backend_.get(), &platform::WifiBackend::connectFailed, this,
              [this] { connecting_ = false; });
    }
  }
  return backend_.get();
}

void Network::refresh() {
  platform::WifiBackend* wifi = backend();
  if (!wifi) return;
  scanning_ = true;
  emit networksChanged();
  wifi->setUpdatesEnabled(true);
  wifi->scan();
}

void Network::onScanCompleted(int status, const QVector<platform::WifiNetwork>& networks) {
  scanning_ = false;
  scanned_.clear();
  if (static_cast<platform::WifiScanStatus>(status) == platform::WifiScanStatus::Success) {
    for (const platform::WifiNetwork& network : networks) {
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
      const bool ca = (a.state & platform::kWifiConnected) != 0;
      const bool cb = (b.state & platform::kWifiConnected) != 0;
      if (ca != cb) return ca;
      return a.quality > b.quality;
    });
  }
  networks_.clear();
  for (const platform::WifiNetwork& network : scanned_) networks_ << networkMap(network);
  emit networksChanged();
}

bool Network::connectTo(const QString& ssid) {
  if (connecting_) return false;
  const auto it =
      std::find_if(scanned_.begin(), scanned_.end(), [&](const auto& n) { return n.ssid == ssid; });
  if (it == scanned_.end() || !it->profileExists) return false;
  platform::WifiBackend* wifi = backend();
  if (!wifi) return false;
  connecting_ = true;
  wifi->connectTo(*it, ssid, QString(), false);
  return true;
}

}  // namespace wabar::services

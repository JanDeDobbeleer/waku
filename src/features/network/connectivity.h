#pragma once

#include <QMetaType>
#include <optional>
#include <string>
#include <vector>

namespace waku::network {

// IANA interface types VPN adapters use: PPP, proprietary virtual (WireGuard, OpenVPN DCO...) and
// tunnel.
bool isVpnInterfaceType(int ianaType);

// The parts of a connection profile needed to tell Wi-Fi, Ethernet and VPN apart.
struct ConnectionSnapshot {
  std::string name;
  bool isWlan = false;
  int ianaType = 0;
  std::optional<int> signalBars;

  bool isVpn() const { return !isWlan && isVpnInterfaceType(ianaType); }
};

// What the Wi-Fi bar widget shows (wifi_managers.WiFiInfo).
struct WifiInfo {
  int bars = 0;
  std::string name;
  int exactQuality = -1;
  bool ethernet = false;
  std::string vpnName;

  bool operator==(const WifiInfo&) const = default;
};

// One poll of the worker: the classified connection plus the host's IPv4 address. `ipAddress` is
// empty when the lookup failed (Python's socket.gethostbyname raising).
struct ConnectivitySample {
  WifiInfo info;
  std::optional<std::string> ipAddress;
};

// Port of classify_connections: builds the widget info from the profiles that have internet access
// and the profile Windows routes internet traffic through. A VPN becomes that profile while
// connected, so the Wi-Fi or Ethernet connection carrying it is looked up among the other online
// profiles.
WifiInfo classifyConnections(const std::vector<ConnectionSnapshot>& online,
                             const std::optional<ConnectionSnapshot>& internet);

// Reads the connection profiles through Windows.Networking.Connectivity and classifies them.
// Returns nullopt when the query throws (Python emits WiFiInfo(0, "Error", -1) then).
std::optional<WifiInfo> queryConnectionInfo();

// First IPv4 address of the local host name (socket.gethostbyname(socket.gethostname())).
std::optional<std::string> localIpAddress();

// Info + IP in one call for the polling worker. `exactQuality` is filled by the caller.
ConnectivitySample sampleConnectivity();

}  // namespace waku::network

Q_DECLARE_METATYPE(waku::network::ConnectivitySample)

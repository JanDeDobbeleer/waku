#pragma once

#include <QMetaType>
#include <cstdint>
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

// One IPv4 address of a network adapter that is up, for choosing the address the bar shows.
struct AdapterAddress {
  std::string address;
  // The adapter has a default gateway: it carries traffic off the local network.
  bool hasGateway = false;
  // The adapter's IPv4 interface metric; lower is preferred.
  std::uint32_t metric = 0;
};

// The address of the adapter Windows prefers for outgoing traffic: gateway adapters first, then the
// lowest metric, then the order given. Link-local (169.254.x.x) and unspecified addresses never win
// over a routable one and are used only when nothing else exists. Nullopt for an empty list.
std::optional<std::string> pickLocalAddress(const std::vector<AdapterAddress>& adapters);

// The IPv4 address of the primary adapter (GetAdaptersAddresses; no name lookup, no network I/O).
std::optional<std::string> localIpAddress();

// Info + IP in one call for the polling worker. `exactQuality` is filled by the caller.
ConnectivitySample sampleConnectivity();

}  // namespace waku::network

Q_DECLARE_METATYPE(waku::network::ConnectivitySample)

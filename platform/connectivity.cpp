#include "platform/connectivity.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>

#include "platform/winrt_apartment.h"

namespace yasb::platform {
namespace {

namespace wnc = winrt::Windows::Networking::Connectivity;

ConnectionSnapshot snapshotOf(const wnc::ConnectionProfile& profile) {
  ConnectionSnapshot snapshot;
  snapshot.name = winrt::to_string(profile.ProfileName());
  snapshot.isWlan = profile.IsWlanConnectionProfile();
  if (const auto adapter = profile.NetworkAdapter())
    snapshot.ianaType = adapter.IanaInterfaceType();
  if (snapshot.isWlan) {
    if (const auto bars = profile.GetSignalBars()) snapshot.signalBars = bars.Value();
  }
  return snapshot;
}

}  // namespace

bool isVpnInterfaceType(int ianaType) {
  return ianaType == 23 || ianaType == 53 || ianaType == 131;
}

WifiInfo classifyConnections(const std::vector<ConnectionSnapshot>& online,
                             const std::optional<ConnectionSnapshot>& internet) {
  const auto wlan = std::find_if(online.begin(), online.end(),
                                 [](const ConnectionSnapshot& p) { return p.isWlan; });
  auto vpn = std::find_if(online.begin(), online.end(),
                          [](const ConnectionSnapshot& p) { return p.isVpn(); });
  const ConnectionSnapshot* vpnProfile = vpn != online.end() ? &*vpn : nullptr;
  bool ethernet = false;
  if (internet && internet->isVpn()) {
    if (!vpnProfile) vpnProfile = &*internet;
    ethernet = wlan == online.end();
  } else {
    ethernet = internet && !internet->isWlan;
  }
  WifiInfo info;
  info.bars = wlan != online.end() && wlan->signalBars ? *wlan->signalBars : 0;
  info.name = wlan != online.end() ? wlan->name : std::string("Disconnected");
  info.exactQuality = -1;
  info.ethernet = ethernet;
  info.vpnName = vpnProfile ? vpnProfile->name : std::string();
  return info;
}

std::optional<WifiInfo> queryConnectionInfo() {
  try {
    ensureWinrtApartment();
    std::vector<ConnectionSnapshot> online;
    for (const auto& profile : wnc::NetworkInformation::GetConnectionProfiles()) {
      if (profile.GetNetworkConnectivityLevel() == wnc::NetworkConnectivityLevel::InternetAccess)
        online.push_back(snapshotOf(profile));
    }
    // Windows returns null while offline.
    std::optional<ConnectionSnapshot> internet;
    if (const auto profile = wnc::NetworkInformation::GetInternetConnectionProfile())
      internet = snapshotOf(profile);
    return classifyConnections(online, internet);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> localIpAddress() {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return std::nullopt;
  std::optional<std::string> result;
  char host[256] = {};
  if (gethostname(host, sizeof(host)) == 0) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* found = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &found) == 0 && found) {
      char text[INET_ADDRSTRLEN] = {};
      const auto* address = reinterpret_cast<const sockaddr_in*>(found->ai_addr);
      if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) result = std::string(text);
      freeaddrinfo(found);
    }
  }
  WSACleanup();
  return result;
}

ConnectivitySample sampleConnectivity() {
  ConnectivitySample sample;
  const auto info = queryConnectionInfo();
  if (info) {
    sample.info = *info;
  } else {
    sample.info.name = "Error";
  }
  sample.ipAddress = localIpAddress();
  return sample;
}

}  // namespace yasb::platform

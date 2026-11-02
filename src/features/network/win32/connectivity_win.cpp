#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winsock2.h>
// iphlpapi.h needs the socket address types first (types only; no Winsock call is made).
#include <iphlpapi.h>

#include <algorithm>
#include <cstdio>
#include <tuple>

#include "features/network/connectivity.h"
#include "kernel/win32/winrt_apartment.h"

namespace waku::network {
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
    waku::kernel::ensureWinrtApartment();
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

std::optional<std::string> pickLocalAddress(const std::vector<AdapterAddress>& adapters) {
  const auto linkLocal = [](const std::string& address) {
    return address.empty() || address.starts_with("169.254.") || address == "0.0.0.0";
  };
  const AdapterAddress* best = nullptr;
  for (const AdapterAddress& adapter : adapters) {
    if (adapter.address.empty()) continue;
    if (!best) {
      best = &adapter;
      continue;
    }
    const auto key = [&](const AdapterAddress& a) {
      return std::tuple(linkLocal(a.address), !a.hasGateway, a.metric);
    };
    if (key(adapter) < key(*best)) best = &adapter;
  }
  if (!best) return std::nullopt;
  return best->address;
}

std::optional<std::string> localIpAddress() {
  // A documented IP helper call over the adapter table: no name resolution and no network I/O, so
  // a poll never waits on DNS or the LLMNR/NetBIOS fallbacks that gethostbyname can fall into.
  constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                          GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
  std::vector<unsigned char> buffer(16 * 1024);
  ULONG size = static_cast<ULONG>(buffer.size());
  ULONG status = ERROR_BUFFER_OVERFLOW;
  for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
    buffer.resize(size);
    status = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                  reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
  }
  if (status != NO_ERROR) return std::nullopt;
  std::vector<AdapterAddress> adapters;
  for (const IP_ADAPTER_ADDRESSES* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
       adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;
    for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
         unicast = unicast->Next) {
      if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
        continue;
      const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
      char text[16] = {};
      std::snprintf(text, sizeof(text), "%u.%u.%u.%u", address->sin_addr.S_un.S_un_b.s_b1,
                    address->sin_addr.S_un.S_un_b.s_b2, address->sin_addr.S_un.S_un_b.s_b3,
                    address->sin_addr.S_un.S_un_b.s_b4);
      adapters.push_back({text, adapter->FirstGatewayAddress != nullptr, adapter->Ipv4Metric});
      break;
    }
  }
  return pickLocalAddress(adapters);
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

}  // namespace waku::network

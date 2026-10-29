#include <windows.h>
#include <winrt/Windows.Devices.WiFi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.h>
#include <wlanapi.h>

#include <QMetaObject>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QThread>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "features/network/wlan.h"
#include "kernel/win32/winrt_apartment.h"

namespace wabar::network {
namespace {

namespace wifi = winrt::Windows::Devices::WiFi;

constexpr DWORD kClientVersion = 2;

QString hiddenNetworkName() { return QStringLiteral("<Hidden Network>"); }

struct WlanMemory {
  void* ptr = nullptr;
  WlanMemory() = default;
  explicit WlanMemory(void* p) : ptr(p) {}
  WlanMemory(const WlanMemory&) = delete;
  WlanMemory& operator=(const WlanMemory&) = delete;
  ~WlanMemory() {
    if (ptr) WlanFreeMemory(ptr);
  }
};

QString decodeSsid(const DOT11_SSID& ssid) {
  const QByteArray bytes(reinterpret_cast<const char*>(ssid.ucSSID),
                         static_cast<qsizetype>(std::min<ULONG>(ssid.uSSIDLength, 32)));
  QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
  const QString text = utf8(bytes);
  if (!utf8.hasError()) return text;
  return QString::fromLatin1(bytes);
}

std::wstring wide(const QString& text) { return text.toStdWString(); }

// A WLAN client handle plus the queries the widget needs. All methods are called from one thread.
class Session {
 public:
  using Callback = void(WINAPI*)(PWLAN_NOTIFICATION_DATA, PVOID);

  explicit Session(Callback callback = nullptr, void* context = nullptr)
      : callback_(callback), context_(context) {}
  ~Session() { close(); }
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool open() {
    if (handle_) return true;
    DWORD version = 0;
    HANDLE handle = nullptr;
    if (WlanOpenHandle(kClientVersion, nullptr, &version, &handle) != ERROR_SUCCESS) return false;
    handle_ = handle;
    if (callback_) {
      if (WlanRegisterNotification(handle_, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, callback_, context_,
                                   nullptr, nullptr) != ERROR_SUCCESS) {
        close();
        return false;
      }
    }
    return true;
  }

  void close() {
    if (!handle_) return;
    if (callback_) {
      WlanRegisterNotification(handle_, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr,
                               nullptr, nullptr);
    }
    WlanCloseHandle(handle_, nullptr);
    handle_ = nullptr;
  }

  HANDLE handle() const { return handle_; }

  std::vector<GUID> interfaces() const {
    std::vector<GUID> result;
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (WlanEnumInterfaces(handle_, nullptr, &list) != ERROR_SUCCESS) return result;
    WlanMemory owner(list);
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
      result.push_back(list->InterfaceInfo[i].InterfaceGuid);
    return result;
  }

  QString profileXml(const GUID& guid, const QString& name) const {
    LPWSTR xml = nullptr;
    const std::wstring profile = wide(name);
    const DWORD status =
        WlanGetProfile(handle_, &guid, profile.c_str(), nullptr, &xml, nullptr, nullptr);
    WlanMemory owner(xml);
    if (status != ERROR_SUCCESS || !xml) return {};
    return QString::fromWCharArray(xml);
  }

  // "auto" is the default when the profile has no connectionMode (or does not exist).
  bool isAutoConnect(const GUID& guid, const QString& name) const {
    static const QRegularExpression mode(QStringLiteral("<connectionMode>(.*?)</connectionMode>"));
    const auto match = mode.match(profileXml(guid, name));
    return !match.hasMatch() || match.captured(1) == QLatin1String("auto");
  }

  bool setAutoConnect(const GUID& guid, const QString& name, bool autoConnect) const {
    static const QRegularExpression mode(QStringLiteral("<connectionMode>.*?</connectionMode>"),
                                         QRegularExpression::CaseInsensitiveOption);
    QString xml = profileXml(guid, name);
    if (xml.isEmpty()) return false;
    xml.replace(mode, QStringLiteral("<connectionMode>%1</connectionMode>")
                          .arg(autoConnect ? QStringLiteral("auto") : QStringLiteral("manual")));
    const std::wstring text = wide(xml);
    DWORD reason = 0;
    return WlanSetProfile(handle_, &guid, 0, text.c_str(), nullptr, TRUE, nullptr, &reason) ==
           ERROR_SUCCESS;
  }

  bool deleteProfile(const GUID& guid, const QString& name) const {
    const std::wstring profile = wide(name);
    return WlanDeleteProfile(handle_, &guid, profile.c_str(), nullptr) == ERROR_SUCCESS;
  }

  // Port of get_available_networks for one interface (deduplicated by SSID, best signal wins).
  void appendAvailable(const GUID& guid, QVector<WifiNetwork>& out) const {
    PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
    if (WlanGetAvailableNetworkList(handle_, &guid, 0, nullptr, &list) != ERROR_SUCCESS) return;
    WlanMemory owner(list);
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
      const WLAN_AVAILABLE_NETWORK& network = list->Network[i];
      QString ssid = decodeSsid(network.dot11Ssid);
      if (ssid.trimmed().isEmpty()) ssid = hiddenNetworkName();
      const auto existing = std::find_if(out.begin(), out.end(),
                                         [&](const WifiNetwork& n) { return n.ssid == ssid; });
      if (existing != out.end()) {
        if (static_cast<int>(network.wlanSignalQuality) > existing->quality)
          existing->quality = static_cast<int>(network.wlanSignalQuality);
        continue;
      }
      WifiNetwork info;
      info.ssid = ssid;
      info.quality = static_cast<int>(network.wlanSignalQuality);
      info.profileExists = (network.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;
      info.autoConnect = isAutoConnect(guid, ssid);
      info.state = 0;
      if (network.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) info.state |= kWifiConnected;
      if (network.bSecurityEnabled) info.state |= kWifiSecured;
      info.authAlg = static_cast<int>(network.dot11DefaultAuthAlgorithm);
      out.push_back(info);
    }
  }

  // Port of get_current_connection / _find_connected_network_info.
  std::optional<WifiNetwork> connected() const {
    for (const GUID& guid : interfaces()) {
      DWORD size = 0;
      PVOID data = nullptr;
      WLAN_OPCODE_VALUE_TYPE type = wlan_opcode_value_type_invalid;
      if (WlanQueryInterface(handle_, &guid, wlan_intf_opcode_current_connection, nullptr, &size,
                             &data, &type) != ERROR_SUCCESS) {
        return std::nullopt;
      }
      WlanMemory dataOwner(data);
      PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
      if (WlanGetAvailableNetworkList(handle_, &guid, 0, nullptr, &list) != ERROR_SUCCESS)
        return std::nullopt;
      WlanMemory listOwner(list);
      for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        const WLAN_AVAILABLE_NETWORK& network = list->Network[i];
        if (!(network.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED)) continue;
        WifiNetwork info;
        info.ssid = decodeSsid(network.dot11Ssid);
        info.quality = static_cast<int>(network.wlanSignalQuality);
        info.state = kWifiConnected | (network.bSecurityEnabled ? kWifiSecured : 0);
        info.autoConnect = isAutoConnect(guid, info.ssid);
        return info;
      }
    }
    return std::nullopt;
  }

 private:
  HANDLE handle_ = nullptr;
  Callback callback_;
  void* context_;
};

wifi::WiFiAdapter firstAdapter() {
  const auto adapters = wifi::WiFiAdapter::FindAllAdaptersAsync().get();
  if (adapters.Size() == 0) throw std::runtime_error("No WiFi adapter found.");
  return adapters.GetAt(0);
}

WifiConnectStatus toStatus(wifi::WiFiConnectionStatus status) {
  switch (status) {
    case wifi::WiFiConnectionStatus::Success:
      return WifiConnectStatus::Success;
    case wifi::WiFiConnectionStatus::AccessRevoked:
      return WifiConnectStatus::AccessRevoked;
    case wifi::WiFiConnectionStatus::InvalidCredential:
      return WifiConnectStatus::InvalidCredential;
    case wifi::WiFiConnectionStatus::NetworkNotAvailable:
      return WifiConnectStatus::NetworkNotAvailable;
    case wifi::WiFiConnectionStatus::Timeout:
      return WifiConnectStatus::Timeout;
    case wifi::WiFiConnectionStatus::UnsupportedAuthenticationProtocol:
      return WifiConnectStatus::UnsupportedAuthenticationProtocol;
    default:
      return WifiConnectStatus::UnspecifiedFailure;
  }
}

// Lives on the backend thread; owns the WLAN handle and runs every blocking call.
class WlanWorker : public QObject {
  Q_OBJECT
 public:
  WlanWorker() : session_(&WlanWorker::notify, this) {}

 public slots:
  void setUpdatesEnabled(bool enabled) { updates_ = enabled; }

  void scan() {
    if (scanning_) return;
    scanning_ = true;
    if (!session_.open()) {
      scanning_ = false;
      emit scanCompleted(static_cast<int>(WifiScanStatus::ServiceUnavailable), {});
      return;
    }
    const std::vector<GUID> interfaces = session_.interfaces();
    if (interfaces.empty()) {
      scanning_ = false;
      emit scanCompleted(static_cast<int>(WifiScanStatus::ServiceUnavailable), {});
      return;
    }
    for (const GUID& guid : interfaces) {
      const DWORD status = WlanScan(session_.handle(), &guid, nullptr, nullptr, nullptr);
      if (status != ERROR_SUCCESS) {
        scanning_ = false;
        WifiScanStatus result = WifiScanStatus::Error;
        if (status == ERROR_ACCESS_DENIED)
          result = WifiScanStatus::AccessDenied;
        else if (status == ERROR_NDIS_DOT11_POWER_STATE_INVALID)
          result = WifiScanStatus::PowerStateInvalid;
        emit scanCompleted(static_cast<int>(result), {});
        return;
      }
    }
  }

  void scanFinished(bool success) {
    if (success && updates_) {
      QVector<WifiNetwork> networks;
      if (session_.open()) {
        for (const GUID& guid : session_.interfaces()) session_.appendAvailable(guid, networks);
      }
      emit scanCompleted(static_cast<int>(WifiScanStatus::Success), networks);
    }
    scanning_ = false;
  }

  void queryCurrent() {
    std::optional<WifiNetwork> network;
    if (session_.open()) network = session_.connected();
    emit currentConnection(network.has_value(), network.value_or(WifiNetwork{}));
  }

  void connectTo(const WifiNetwork& network, const QString& ssid, const QString& password,
                 bool hidden) {
    try {
      wabar::kernel::ensureWinrtApartment();
      const WifiConnectStatus status = connectAdapter(network, ssid, password, hidden);
      emit connectFinished(static_cast<int>(status), ssid, network);
    } catch (const winrt::hresult_error& error) {
      emit connectFailed(QString::fromStdWString(error.message().c_str()));
    } catch (const std::exception& error) {
      emit connectFailed(QString::fromUtf8(error.what()));
    }
  }

  void disconnectCurrent() {
    try {
      wabar::kernel::ensureWinrtApartment();
      firstAdapter().Disconnect();
    } catch (...) {
    }
  }

  void forget(const QString& profileName) {
    if (!session_.open()) return;
    for (const GUID& guid : session_.interfaces()) session_.deleteProfile(guid, profileName);
  }

  void setAutoConnect(const QString& ssid, bool autoConnect) {
    if (!session_.open()) return;
    const auto current = session_.connected();
    const bool active = current && current->ssid == ssid;
    if (active) disconnectCurrent();
    for (const GUID& guid : session_.interfaces()) {
      if (session_.isAutoConnect(guid, ssid) == autoConnect) continue;
      if (session_.setAutoConnect(guid, ssid, autoConnect)) break;
    }
    if (active) {
      WifiNetwork network;
      network.ssid = ssid;
      network.autoConnect = autoConnect;
      connectTo(network, ssid, QString(), false);
    }
  }

  void shutdown() { session_.close(); }

  void profileDisconnected(const QString& profile) { emit disconnected(profile); }

 signals:
  void scanCompleted(int status, const QVector<wabar::network::WifiNetwork>& networks);
  void currentConnection(bool connected, const wabar::network::WifiNetwork& network);
  void connectFinished(int status, const QString& ssid, const wabar::network::WifiNetwork& network);
  void connectFailed(const QString& message);
  void disconnected(const QString& profileName);

 private:
  WifiConnectStatus connectAdapter(const WifiNetwork& network, const QString& ssid,
                                   const QString& password, bool hidden) {
    const auto adapter = firstAdapter();
    const auto report = adapter.NetworkReport();
    const auto kind = network.autoConnect ? wifi::WiFiReconnectionKind::Automatic
                                          : wifi::WiFiReconnectionKind::Manual;
    if (!hidden) {
      for (const auto& available : report.AvailableNetworks()) {
        if (QString::fromStdWString(available.Ssid().c_str()) != ssid) continue;
        if (!password.isEmpty()) {
          winrt::Windows::Security::Credentials::PasswordCredential credential;
          credential.Password(winrt::hstring(wide(password)));
          return toStatus(
              adapter.ConnectAsync(available, kind, credential).get().ConnectionStatus());
        }
        return toStatus(adapter.ConnectAsync(available, kind).get().ConnectionStatus());
      }
      throw std::runtime_error("Selected network not found after scan.");
    }
    for (const auto& available : report.AvailableNetworks()) {
      if (!available.Ssid().empty()) continue;
      winrt::Windows::Security::Credentials::PasswordCredential credential;
      credential.Password(winrt::hstring(wide(password)));
      return toStatus(adapter.ConnectAsync(available, kind, credential, winrt::hstring(wide(ssid)))
                          .get()
                          .ConnectionStatus());
    }
    throw std::runtime_error("No hidden network found after scan.");
  }

  // Runs on a WLAN thread-pool thread: only forward to the worker thread.
  static void WINAPI notify(PWLAN_NOTIFICATION_DATA data, PVOID context) {
    if (!data || data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM) return;
    auto* worker = static_cast<WlanWorker*>(context);
    switch (data->NotificationCode) {
      case wlan_notification_acm_scan_complete:
        QMetaObject::invokeMethod(
            worker, [worker] { worker->scanFinished(true); }, Qt::QueuedConnection);
        break;
      case wlan_notification_acm_scan_fail:
        QMetaObject::invokeMethod(
            worker, [worker] { worker->scanFinished(false); }, Qt::QueuedConnection);
        break;
      case wlan_notification_acm_disconnected: {
        if (data->dwDataSize < sizeof(WLAN_CONNECTION_NOTIFICATION_DATA) || !data->pData) break;
        const auto* info = static_cast<const WLAN_CONNECTION_NOTIFICATION_DATA*>(data->pData);
        const QString profile = QString::fromWCharArray(info->strProfileName);
        QMetaObject::invokeMethod(
            worker, [worker, profile] { worker->profileDisconnected(profile); },
            Qt::QueuedConnection);
        break;
      }
      default:
        break;
    }
  }

  Session session_;
  bool updates_ = false;
  bool scanning_ = false;
};

// GUI-thread facade: owns the worker thread and forwards calls to it.
class WlanBackend final : public WifiBackend {
 public:
  WlanBackend() {
    worker_ = new WlanWorker;
    worker_->moveToThread(&thread_);
    connect(worker_, &WlanWorker::scanCompleted, this, &WifiBackend::scanCompleted);
    connect(worker_, &WlanWorker::currentConnection, this, &WifiBackend::currentConnection);
    connect(worker_, &WlanWorker::connectFinished, this, &WifiBackend::connectFinished);
    connect(worker_, &WlanWorker::connectFailed, this, &WifiBackend::connectFailed);
    connect(worker_, &WlanWorker::disconnected, this, &WifiBackend::disconnected);
    thread_.setObjectName(QStringLiteral("WlanBackend"));
    thread_.start();
  }

  ~WlanBackend() override {
    if (thread_.isRunning()) {
      QMetaObject::invokeMethod(worker_, &WlanWorker::shutdown, Qt::BlockingQueuedConnection);
      thread_.quit();
      thread_.wait();
    }
    delete worker_;
  }

  void setUpdatesEnabled(bool enabled) override {
    QMetaObject::invokeMethod(
        worker_, [w = worker_, enabled] { w->setUpdatesEnabled(enabled); }, Qt::QueuedConnection);
  }
  void scan() override {
    QMetaObject::invokeMethod(worker_, &WlanWorker::scan, Qt::QueuedConnection);
  }
  void queryCurrent() override {
    QMetaObject::invokeMethod(worker_, &WlanWorker::queryCurrent, Qt::QueuedConnection);
  }
  void connectTo(const WifiNetwork& network, const QString& ssid, const QString& password,
                 bool hidden) override {
    QMetaObject::invokeMethod(
        worker_,
        [w = worker_, network, ssid, password, hidden] {
          w->connectTo(network, ssid, password, hidden);
        },
        Qt::QueuedConnection);
  }
  void disconnectCurrent() override {
    QMetaObject::invokeMethod(worker_, &WlanWorker::disconnectCurrent, Qt::QueuedConnection);
  }
  void forget(const QString& profileName) override {
    QMetaObject::invokeMethod(
        worker_, [w = worker_, profileName] { w->forget(profileName); }, Qt::QueuedConnection);
  }
  void setAutoConnect(const QString& ssid, bool autoConnect) override {
    QMetaObject::invokeMethod(
        worker_, [w = worker_, ssid, autoConnect] { w->setAutoConnect(ssid, autoConnect); },
        Qt::QueuedConnection);
  }

 private:
  QThread thread_;
  WlanWorker* worker_ = nullptr;
};

}  // namespace

std::unique_ptr<WifiBackend> createWlanBackend() { return std::make_unique<WlanBackend>(); }

std::optional<int> currentSignalQuality() {
  Session session;
  if (!session.open()) return std::nullopt;
  const auto network = session.connected();
  if (!network) return std::nullopt;
  return network->quality;
}

}  // namespace wabar::network

#include "wlan_win.moc"

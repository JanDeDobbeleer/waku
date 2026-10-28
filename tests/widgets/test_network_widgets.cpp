#include <gtest/gtest.h>

#include <QApplication>
#include <QLayout>
#include <QScrollBar>
#include <fstream>

#include "core/bar/theme_class.h"
#include "core/config/schema_validator.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/widget_context.h"
#include "platform/bluetooth.h"
#include "platform/connectivity.h"
#include "platform/wlan.h"
#include "widgets/bluetooth/bluetooth_menu.h"
#include "widgets/bluetooth/bluetooth_widget.h"
#include "widgets/wifi/wifi_menu.h"
#include "widgets/wifi/wifi_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

// --- fakes: no radio, network or device is ever touched -------------------------------------

class FakeWifiBackend : public WifiBackend {
 public:
  void setUpdatesEnabled(bool enabled) override { updates = enabled; }
  void scan() override { ++scans; }
  void queryCurrent() override { ++queries; }
  void connectTo(const WifiNetwork& network, const QString& ssid, const QString& password,
                 bool hidden) override {
    connects.push_back({network.ssid, ssid, password, hidden});
  }
  void disconnectCurrent() override { ++disconnects; }
  void forget(const QString& profile) override { forgotten << profile; }
  void setAutoConnect(const QString& ssid, bool value) override {
    autoConnects.push_back({ssid, value});
  }

  struct Connect {
    QString networkSsid, ssid, password;
    bool hidden;
  };
  bool updates = false;
  int scans = 0, queries = 0, disconnects = 0;
  std::vector<Connect> connects;
  QStringList forgotten;
  std::vector<std::pair<QString, bool>> autoConnects;
};

class FakeBluetoothBackend : public BluetoothBackend {
 public:
  void start() override { ++starts; }
  void refresh() override { ++refreshes; }
  void scan() override { ++scans; }
  bool setRadio(bool on) override {
    radioRequests.push_back(on);
    return allowRadio;
  }
  void connectDevice(const QString& address) override { connected << address; }
  void disconnectDevice(const QString& address) override { disconnected << address; }
  void publish(const BluetoothStatus& status) { publishStatus(status); }

  int starts = 0, refreshes = 0, scans = 0;
  bool allowRadio = true;
  std::vector<bool> radioRequests;
  QStringList connected, disconnected;
};

class NetworkWidgetTest : public testing::Test {
 protected:
  // Options are normalized like the config loader does (defaults filled in).
  template <class Config>
  WidgetContext context(const char* name, const char* type, const json& raw) {
    const auto validated = yasb::config::SchemaValidator::embedded().validateWidgetOptions(
        type, yasb::config::OJson::parse(raw.dump()));
    EXPECT_TRUE(validated.ok());
    const json options = json::parse(validated.value.dump());
    return WidgetContext{name,
                         type,
                         options,
                         {},
                         {},
                         eventBus_,
                         globalState_,
                         pollingWorkers_,
                         options.get<Config>()};
  }

  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
};

json labelState(const QLabel* label) {
  return {{"class", label->property("class").toString().toStdString()},
          {"hidden", label->isHidden()},
          {"text", label->text().toStdString()}};
}

json labelStates(const std::vector<QLabel*>& labels) {
  json result = json::array();
  for (const QLabel* label : labels) result.push_back(labelState(label));
  return result;
}

// Same shape as tools/parity label_registry.snapshot (ethernet groups only for the wifi widget).
json snapshot(BaseWidget& widget, const std::vector<QLabel*>* ethernet = nullptr,
              const std::vector<QLabel*>* ethernetAlt = nullptr) {
  json order = json::array();
  QLayout* layout = widget.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const auto* label = qobject_cast<QLabel*>(layout->itemAt(i)->widget());
    const auto has = [&](const std::vector<QLabel*>& v) {
      return std::find(v.begin(), v.end(), label) != v.end();
    };
    order.push_back(has(widget.labels()) ? "primary" : has(widget.labelsAlt()) ? "alt" : "other");
  }
  json result = {
      {"alt", labelStates(widget.labelsAlt())},
      {"container_class", widget.labelContainer()->property("class").toString().toStdString()},
      {"frame_class", widget.widgetFrame()->property("class").toString().toStdString()},
      {"layout_order", order},
      {"primary", labelStates(widget.labels())},
      {"widget_hidden", widget.isHidden()}};
  if (ethernet) result["ethernet"] = labelStates(*ethernet);
  if (ethernetAlt) result["ethernet_alt"] = labelStates(*ethernetAlt);
  return result;
}

json loadVectors(const std::string& file) {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/" + file, std::ios::binary);
  EXPECT_TRUE(in.good());
  return json::parse(in);
}

ConnectionSnapshot wlan() { return {"Home", true, 71, 3}; }
ConnectionSnapshot ethernet() { return {"Network", false, 6, std::nullopt}; }
ConnectionSnapshot wireguard() { return {"ProtonVPN", false, 53, std::nullopt}; }

}  // namespace

// --- connection classification (yasb:tests/widgets/test_wifi.py) -----------------------------

TEST(Connectivity, VpnOverWifiKeepsTheWifiNetwork) {
  const WifiInfo info = classifyConnections({wireguard(), wlan()}, wireguard());
  EXPECT_FALSE(info.ethernet);
  EXPECT_EQ(info.name, "Home");
  EXPECT_EQ(info.bars, 3);
  EXPECT_EQ(info.vpnName, "ProtonVPN");
}

TEST(Connectivity, VpnOverEthernetShowsEthernet) {
  const WifiInfo info = classifyConnections({wireguard(), ethernet()}, wireguard());
  EXPECT_TRUE(info.ethernet);
  EXPECT_EQ(info.vpnName, "ProtonVPN");
}

TEST(Connectivity, WithoutVpn) {
  WifiInfo info = classifyConnections({wlan()}, wlan());
  EXPECT_FALSE(info.ethernet);
  EXPECT_EQ(info.name, "Home");
  EXPECT_EQ(info.vpnName, "");
  info = classifyConnections({ethernet(), wlan()}, ethernet());
  EXPECT_TRUE(info.ethernet);
  EXPECT_EQ(info.name, "Home");
  info = classifyConnections({}, std::nullopt);
  EXPECT_FALSE(info.ethernet);
  EXPECT_EQ(info.name, "Disconnected");
  EXPECT_EQ(info.bars, 0);
}

TEST(Connectivity, VpnInterfaceTypes) {
  for (int type : {23, 53, 131}) EXPECT_TRUE(isVpnInterfaceType(type));
  for (int type : {6, 71, 0}) EXPECT_FALSE(isVpnInterfaceType(type));
}

// --- label vectors replayed against the real widgets ----------------------------------------

TEST_F(NetworkWidgetTest, WifiLabelsMatchPython) {
  const json cases = loadVectors("wifi.json");
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    QWidget host;
    WifiWidget widget(
        context<yasb::config::options::WifiConfig>("wifi", "yasb.wifi.WifiWidget", c.at("options")),
        false, [] { return std::make_unique<FakeWifiBackend>(); },
        [] { return std::optional<std::string>("192.0.2.10"); });
    widget.setParent(&host);
    widget.show();
    const auto snap = [&] {
      return snapshot(widget, &widget.ethernetLabels(), &widget.ethernetLabelsAlt());
    };
    EXPECT_EQ(snap(), c.at("initial")) << json::diff(snap(), c.at("initial")).dump(1);
    size_t i = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(i++) + " " + step.dump());
      if (step.value("toggle", false)) {
        widget.runCallback("toggle_label");
      } else {
        const json& spec = step.at("wifi");
        ConnectivitySample sample;
        sample.info.bars = spec.value("bars", 0);
        sample.info.name = spec.value("name", "Disconnected");
        sample.info.exactQuality = spec.value("exact", -1);
        sample.info.ethernet = spec.value("ethernet", false);
        sample.info.vpnName = spec.value("vpn", "");
        if (!spec.contains("ip"))
          sample.ipAddress = "192.0.2.10";
        else if (!spec.at("ip").is_null())
          sample.ipAddress = spec.at("ip").get<std::string>();
        widget.applySample(sample);
      }
      EXPECT_EQ(snap(), s.at("snapshot")) << json::diff(snap(), s.at("snapshot")).dump(1);
    }
  }
}

TEST_F(NetworkWidgetTest, BluetoothLabelsMatchPython) {
  const json cases = loadVectors("bluetooth.json");
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(c.at("scenario").get<std::string>());
    QWidget host;
    auto backend = std::make_shared<FakeBluetoothBackend>();
    BluetoothWidget widget(context<yasb::config::options::BluetoothConfig>(
                               "bluetooth", "yasb.bluetooth.BluetoothWidget", c.at("options")),
                           backend);
    widget.setParent(&host);
    widget.show();
    EXPECT_EQ(snapshot(widget), c.at("initial"))
        << json::diff(snapshot(widget), c.at("initial")).dump(1);
    size_t i = 0;
    for (const json& s : c.at("steps")) {
      const json& step = s.at("step");
      SCOPED_TRACE("step " + std::to_string(i++) + " " + step.dump());
      if (step.value("toggle", false)) {
        widget.runCallback("toggle_label");
      } else {
        const json& spec = step.at("status");
        BluetoothStatus status;
        status.radioOn = spec.at("radio_on").get<bool>();
        int index = 0;
        for (const json& d : spec.at("devices")) {
          BluetoothDevice device;
          device.name = QString::fromStdString(d.at("name").get<std::string>());
          device.address =
              QStringLiteral("AA:BB:CC:00:00:%1").arg(index++, 2, 16, QLatin1Char('0'));
          device.connected = d.value("connected", false);
          device.paired = d.value("paired", true);
          device.remembered = d.value("remembered", false);
          status.devices.push_back(device);
        }
        widget.applyStatus(status);
      }
      EXPECT_EQ(snapshot(widget), s.at("snapshot"))
          << json::diff(snapshot(widget), s.at("snapshot")).dump(1);
    }
  }
}

// --- Wi-Fi menu (fake backend) ---------------------------------------------------------------

namespace {
WifiMenuOptions menuOptions() {
  WifiMenuOptions options;
  options.blur = false;
  options.iconsSecured = {"s0", "s1", "s2", "s3"};
  options.iconsUnsecured = {"u0", "u1", "u2", "u3"};
  return options;
}
}  // namespace

TEST_F(NetworkWidgetTest, WifiMenuListsNetworksSortedWithConnectedFirst) {
  QWidget anchor;
  FakeWifiBackend* backend = nullptr;
  WifiMenu menu(&anchor, menuOptions(), [&] {
    auto created = std::make_unique<FakeWifiBackend>();
    backend = created.get();
    return created;
  });
  menu.setUriOpener([](const QString&) {});
  anchor.show();
  menu.showMenu();
  ASSERT_NE(backend, nullptr);
  EXPECT_TRUE(backend->updates);
  EXPECT_EQ(backend->scans, 1);
  EXPECT_EQ(backend->queries, 1);

  WifiNetwork weak;
  weak.ssid = "Weak";
  weak.quality = 20;
  weak.state = kWifiSecured;
  WifiNetwork strong;
  strong.ssid = "Strong";
  strong.quality = 90;
  strong.state = kWifiUnsecured;
  WifiNetwork current;
  current.ssid = "Current";
  current.quality = 50;
  current.state = kWifiConnected | kWifiSecured;
  emit backend->scanCompleted(static_cast<int>(WifiScanStatus::Success), {weak, strong, current});
  for (int i = 0; i < 5; ++i) QApplication::processEvents();
  // The 100 ms debounce timer drives the list update; call the same path directly.
  emit backend->currentConnection(true, current);

  ASSERT_NE(menu.list(), nullptr);
  const auto& items = menu.list()->items();
  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0]->data().ssid, "Current");
  EXPECT_EQ(items[1]->data().ssid, "Strong");
  EXPECT_EQ(items[2]->data().ssid, "Weak");
  EXPECT_EQ(items[1]->iconLabel()->text(), "u3");
  EXPECT_EQ(items[0]->statusLabel()->text(), "Connected, Secured");
  EXPECT_EQ(items[2]->statusLabel()->text(), "Secured");
  EXPECT_EQ(items[1]->statusLabel()->text(), "Unsecured");
  EXPECT_EQ(items[1]->strengthLabel()->text(), "90%");
  menu.popup()->hideNow();
}

TEST_F(NetworkWidgetTest, WifiMenuRadioOffShowsErrorAndClearsList) {
  QWidget anchor;
  WifiMenu menu(&anchor, menuOptions(), [] { return std::make_unique<FakeWifiBackend>(); });
  menu.setUriOpener([](const QString&) {});
  anchor.show();
  menu.showMenu();
  auto* backend = static_cast<FakeWifiBackend*>(menu.backend());
  emit backend->scanCompleted(static_cast<int>(WifiScanStatus::PowerStateInvalid), {});
  EXPECT_EQ(menu.errorMessage()->text(), "Error: WiFi adapter is disabled...");
  EXPECT_TRUE(menu.errorMessage()->clickable());
  EXPECT_FALSE(menu.errorMessage()->isHidden());
  EXPECT_TRUE(menu.cache().empty());
  menu.popup()->hideNow();
}

TEST_F(NetworkWidgetTest, WifiMenuConnectUsesPasswordAndHiddenSsid) {
  QWidget anchor;
  WifiMenu menu(&anchor, menuOptions(), [] { return std::make_unique<FakeWifiBackend>(); });
  menu.setUriOpener([](const QString&) {});
  anchor.show();
  menu.showMenu();
  auto* backend = static_cast<FakeWifiBackend*>(menu.backend());
  WifiNetwork hidden;
  hidden.ssid = "<Hidden Network>";
  hidden.quality = 10;
  hidden.state = kWifiSecured;
  emit backend->scanCompleted(static_cast<int>(WifiScanStatus::Success), {hidden});
  emit backend->currentConnection(false, {});
  WifiItem* item = menu.list()->item("<Hidden Network>");
  ASSERT_NE(item, nullptr);
  menu.list()->onItemClicked(item);
  item->ssidField()->setText("Secret");
  item->passwordField()->setText("pw");
  item->connectButton()->click();
  ASSERT_EQ(backend->connects.size(), 1u);
  EXPECT_EQ(backend->connects[0].ssid, "Secret");
  EXPECT_EQ(backend->connects[0].password, "pw");
  EXPECT_TRUE(backend->connects[0].hidden);
  menu.popup()->hideNow();
}

TEST_F(NetworkWidgetTest, ScrollingListKeepsTheExpandedItemAtFullHeight) {
  // A CSS min-height becomes each item's minimum, which the scroll area squeezed every item to.
  QWidget host;
  host.setStyleSheet(
      "[class~=\"wifi-item\"] { min-height: 44px; padding: 6px 12px; }"
      "[class~=\"wifi-item\"] [class~=\"connect\"] { padding: 6px 14px; }");
  WifiList list(&host);
  list.resize(375, 200);
  for (int i = 0; i < 8; ++i) {
    WifiNetwork network;
    network.ssid = QStringLiteral("Network %1").arg(i);
    network.state = kWifiSecured;
    list.updateOrAddItem(network, false);
  }
  host.show();
  WifiItem* expanded = list.item("Network 0");
  ASSERT_NE(expanded, nullptr);
  list.onItemClicked(expanded);
  for (int i = 0; i < 3; ++i) QApplication::processEvents();
  EXPECT_GT(list.verticalScrollBar()->maximum(), 0);
  EXPECT_GE(expanded->height(), expanded->sizeHint().height());
}

// --- Bluetooth -------------------------------------------------------------------------------

TEST(BluetoothTypes, ClassOfDeviceMapping) {
  EXPECT_EQ(formatBtAddress(0xAABBCC001122ULL), "AA:BB:CC:00:11:22");
  EXPECT_EQ(parseClassOfDevice(0x240404), (std::pair<int, int>{4, 1}));
  EXPECT_EQ(deviceTypeFromCod(4, 1), BtDeviceType::Headset);
  EXPECT_EQ(deviceTypeFromCod(4, 6), BtDeviceType::Headphones);
  EXPECT_EQ(deviceTypeFromCod(4, 5), BtDeviceType::Speaker);
  EXPECT_EQ(deviceTypeFromCod(2, 0), BtDeviceType::Phone);
  EXPECT_EQ(deviceTypeFromCod(5, 0x10), BtDeviceType::Keyboard);
  EXPECT_EQ(deviceTypeFromCod(5, 0x20), BtDeviceType::Mouse);
  EXPECT_EQ(deviceTypeFromCod(5, 0x02), BtDeviceType::Controller);
  EXPECT_EQ(deviceTypeFromCod(7, 0), BtDeviceType::Watch);
  EXPECT_EQ(deviceTypeFromCod(6, 0x08), BtDeviceType::Camera);
  EXPECT_EQ(deviceTypeFromCod(99, 0), BtDeviceType::Generic);
  EXPECT_EQ(profilesFromCod(4, true), (QStringList{"mic", "audio"}));
  EXPECT_TRUE(profilesFromCod(4, false).isEmpty());
  EXPECT_EQ(profilesFromCod(5, true), QStringList{"input"});
}

TEST(BluetoothTypes, PreferKnownThenConnected) {
  BluetoothDevice known;
  known.paired = true;
  BluetoothDevice unknown;
  unknown.connected = true;
  EXPECT_TRUE(preferDevice(known, unknown));
  EXPECT_FALSE(preferDevice(unknown, known));
  BluetoothDevice a, b;
  b.connected = true;
  EXPECT_TRUE(preferDevice(b, a));
  EXPECT_FALSE(preferDevice(a, b));
}

namespace {
BluetoothDevice headphones(bool connected, std::optional<int> battery) {
  BluetoothDevice device;
  device.name = "Buds";
  device.address = "AA:BB:CC:00:00:01";
  device.addressInt = 1;
  device.paired = true;
  device.connected = connected;
  device.deviceType = BtDeviceType::Headphones;
  device.battery = battery;
  device.profiles = connected ? QStringList{"mic", "audio"} : QStringList{};
  return device;
}

json bluetoothMenuOptions() {
  return {{"label", "<span>{icon}</span>"},
          {"menu_config",
           {{"blur", false},
            {"labels", {{"connected", "Connected"}, {"not_connected", "Not connected"}}}}}};
}
}  // namespace

TEST_F(NetworkWidgetTest, BluetoothMenuShowsBatteryOnlyWhenKnown) {
  auto backend = std::make_shared<FakeBluetoothBackend>();
  BluetoothWidget widget(context<yasb::config::options::BluetoothConfig>(
                             "bluetooth", "yasb.bluetooth.BluetoothWidget", bluetoothMenuOptions()),
                         backend);
  widget.show();
  BluetoothMenu* menu = widget.menu();
  menu->setUriOpener([](const QString&) {});
  menu->showMenu();
  EXPECT_EQ(backend->refreshes, 1);
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {headphones(true, 55)};
  backend->publish(status);
  BluetoothItem* item = menu->list()->findItem("AA:BB:CC:00:00:01");
  ASSERT_NE(item, nullptr);
  EXPECT_FALSE(item->batteryLabel()->isHidden());
  EXPECT_EQ(item->batteryLabel()->text(), "55%");
  EXPECT_EQ(item->batteryIcon()->property("class").toString(), "battery-icon medium");
  EXPECT_EQ(item->statusLabel()->text(), "Connected mic, audio");

  status.devices = {headphones(true, std::nullopt)};
  backend->publish(status);
  EXPECT_TRUE(item->batteryLabel()->isHidden());
  EXPECT_TRUE(item->batteryIcon()->isHidden());

  status.devices = {headphones(false, 80)};
  backend->publish(status);
  EXPECT_TRUE(item->batteryLabel()->isHidden());
  EXPECT_EQ(item->statusLabel()->text(), "Not connected");
  menu->popup()->hideNow();
}

TEST_F(NetworkWidgetTest, BluetoothMenuConnectAndPowerGoThroughBackend) {
  auto backend = std::make_shared<FakeBluetoothBackend>();
  BluetoothWidget widget(context<yasb::config::options::BluetoothConfig>(
                             "bluetooth", "yasb.bluetooth.BluetoothWidget", bluetoothMenuOptions()),
                         backend);
  widget.show();
  BluetoothMenu* menu = widget.menu();
  menu->showMenu();
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {headphones(false, std::nullopt)};
  backend->publish(status);
  BluetoothItem* item = menu->list()->findItem("AA:BB:CC:00:00:01");
  ASSERT_NE(item, nullptr);
  item->setActive(true);
  item->connectButton()->click();
  EXPECT_EQ(backend->connected, QStringList{"AA:BB:CC:00:00:01"});
  EXPECT_TRUE(item->busy());
  emit backend->connectionFinished(false, "Failed to connect device",
                                   headphones(false, std::nullopt));
  EXPECT_FALSE(item->busy());
  EXPECT_EQ(menu->errorLabel()->text(), "Failed to connect device");

  backend->allowRadio = false;
  menu->powerButton()->setChecked(false);
  EXPECT_EQ(backend->radioRequests, std::vector<bool>{false});
  EXPECT_TRUE(menu->powerButton()->isChecked());  // reverted
  EXPECT_EQ(menu->errorLabel()->text(), "Unable to change Bluetooth power");
  menu->popup()->hideNow();
}

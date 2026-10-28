#include "widgets/wifi/wifi_widget.h"

#include <QLayout>
#include <QVariant>
#include <algorithm>
#include <stdexcept>
#include <utility>

#include "core/widgets/polling_worker_registry.h"
#include "widgets/menu_support/label_parts.h"
#include "widgets/system_widget_helpers.h"
#include "widgets/wifi/wifi_menu.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

}  // namespace

WifiWidget::WifiWidget(const WidgetContext& context, bool startPolling,
                       BackendFactory backendFactory, IpResolver ipResolver)
    : BaseWidget(qs(context.name), context.eventBus,
                 detail::className(std::get<config::options::WifiConfig>(context.typedOptions),
                                   "wifi-widget "),
                 0),
      options_(std::get<config::options::WifiConfig>(context.typedOptions)) {
  buildWidgetLabel(qs(options_.label), qs(options_.label_alt));
  ethernetLabels_ = buildLabels(qs(options_.ethernet_label), false, true);
  ethernetLabelsAlt_ = buildLabels(qs(options_.ethernet_label_alt), true, true);

  const auto& menu = options_.menu_config;
  WifiMenuOptions menuOptions;
  menuOptions.blur = menu.blur;
  menuOptions.roundCorners = menu.round_corners;
  menuOptions.roundCornersType = qs(menu.round_corners_type);
  menuOptions.borderColor = qs(menu.border_color);
  menuOptions.alignment = qs(menu.alignment);
  menuOptions.direction = qs(menu.direction);
  menuOptions.offsetLeft = menu.offset_left;
  menuOptions.offsetTop = menu.offset_top;
  for (const auto& icon : menu.wifi_icons_secured) menuOptions.iconsSecured << qs(icon);
  for (const auto& icon : menu.wifi_icons_unsecured) menuOptions.iconsUnsecured << qs(icon);
  menu_ = new WifiMenu(this, menuOptions, std::move(backendFactory));

  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"),
                   [this](const QStringList&) { menu_->showMenu(); });
  setCallbackBinding({qs(options_.callbacks.on_left), qs(options_.callbacks.on_middle),
                      qs(options_.callbacks.on_right), QStringLiteral("do_nothing")});

  // Python resolves the host address on every update; here the first render looks it up once and
  // the poll worker keeps it fresh.
  ip_ = ipResolver ? ipResolver() : platform::localIpAddress();
  render();

  if (startPolling) {
    const bool exact = options_.get_exact_wifi_strength;
    const int interval = std::max(options_.update_interval, 100);
    if (PollingWorker* worker =
            context.pollingWorkers.get(exact ? "wifi-exact" : "wifi", interval, [exact] {
              return [exact] {
                platform::ConnectivitySample sample = platform::sampleConnectivity();
                if (exact) {
                  if (const auto quality = platform::currentSignalQuality())
                    sample.info.exactQuality = *quality;
                }
                return QVariant::fromValue(sample);
              };
            })) {
      connect(worker, &PollingWorker::dataReady, this, [this](const QVariant& value) {
        if (value.canConvert<platform::ConnectivitySample>())
          applySample(value.value<platform::ConnectivitySample>());
      });
      worker->start();
    }
  }
}

WifiWidget::~WifiWidget() = default;

std::vector<QLabel*> WifiWidget::buildLabels(const QString& content, bool isAlt, bool isEthernet) {
  std::vector<QLabel*> result;
  for (const LabelPart& part : splitLabelParts(content, isAlt)) {
    auto* label = new QLabel(part.text, labelContainer());
    label->setProperty("class", part.cssClass);
    label->setAlignment(Qt::AlignCenter);
    labelContainer()->layout()->addWidget(label);
    result.push_back(label);
    label->setVisible(!(isAlt || isEthernet));
  }
  return result;
}

void WifiWidget::applySample(const platform::ConnectivitySample& sample) {
  info_ = sample.info;
  ip_ = sample.ipAddress;
  render();
}

void WifiWidget::toggleLabel() {
  showAltLabel_ = !showAltLabel_;
  render();
}

QString WifiWidget::wifiIcon(int strength) const {
  int bars = 0;
  if (strength >= 80)
    bars = 4;
  else if (strength >= 60)
    bars = 3;
  else if (strength >= 40)
    bars = 2;
  else if (strength >= 20)
    bars = 1;
  const auto& icons = options_.wifi_icons;
  if (bars >= static_cast<int>(icons.size())) throw std::out_of_range("wifi_icons");
  return qs(icons[static_cast<size_t>(bars)]);
}

void WifiWidget::displayCorrectLabel() {
  struct Group {
    bool active;
    const std::vector<QLabel*>& primary;
    const std::vector<QLabel*>& alt;
  };
  const bool alt = showAltLabel_;
  const Group groups[] = {{!ethernetActive_, labels(), labelsAlt()},
                          {ethernetActive_, ethernetLabels_, ethernetLabelsAlt_}};
  for (const Group& group : groups) {
    for (QLabel* label : group.primary) label->setVisible(group.active && !alt);
    for (QLabel* label : group.alt) label->setVisible(group.active && alt);
  }
}

void WifiWidget::render() {
  const QString vpnName = qs(info_.vpnName);
  const QString vpnIcon = vpnName.isEmpty() ? QString() : qs(options_.vpn_icon);
  QString ipAddr;
  QString icon;
  QString name;
  QString strength;
  bool failed = !ip_.has_value();
  if (!failed) {
    ipAddr = qs(*ip_);
    try {
      if (!info_.ethernet) {
        const bool wasEthernet = ethernetActive_;
        ethernetActive_ = false;
        if (wasEthernet && options_.hide_if_ethernet) show();
        const int value = options_.get_exact_wifi_strength && info_.exactQuality >= 0
                              ? info_.exactQuality
                              : std::min(info_.bars, 4) * 25;
        icon = wifiIcon(value);
        name = qs(info_.name);
        strength = QString::number(value);
      } else {
        ethernetActive_ = true;
        if (options_.hide_if_ethernet) {
          hide();
          return;
        }
        icon = qs(options_.ethernet_icon);
        name = QStringLiteral("Ethernet");
        strength = QStringLiteral("N/A");
      }
    } catch (const std::exception&) {
      failed = true;
    }
  }
  if (failed) {
    ipAddr = QStringLiteral("N/A");
    icon = name = strength = QStringLiteral("N/A");
  }

  displayCorrectLabel();
  const bool alt = showAltLabel_;
  const std::vector<QLabel*>& active = ethernetActive_
                                           ? (alt ? ethernetLabelsAlt_ : ethernetLabels_)
                                           : (alt ? labelsAlt() : labels());
  const std::string& content = ethernetActive_
                                   ? (alt ? options_.ethernet_label_alt : options_.ethernet_label)
                                   : (alt ? options_.label_alt : options_.label);
  const std::vector<std::pair<QString, QString>> replacements = {
      {QStringLiteral("{wifi_icon}"), icon},         {QStringLiteral("{wifi_name}"), name},
      {QStringLiteral("{wifi_strength}"), strength}, {QStringLiteral("{ip_addr}"), ipAddr},
      {QStringLiteral("{vpn_icon}"), vpnIcon},       {QStringLiteral("{vpn_name}"), vpnName}};
  size_t index = 0;
  for (const QString& part : rawLabelParts(qs(content))) {
    const QString text = replacePlaceholders(part, replacements);
    if (index < active.size()) {
      active[index]->setText(text);
      // Hide parts that format to nothing, e.g. a {vpn_icon} span while no VPN is connected.
      if (isBlankAfterSpans(text)) active[index]->hide();
    }
    ++index;
  }
}

}  // namespace yasb::widgets

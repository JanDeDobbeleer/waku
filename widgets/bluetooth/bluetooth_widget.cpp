#include "widgets/bluetooth/bluetooth_widget.h"

#include <QLayout>
#include <QRegularExpression>
#include <mutex>

#include "core/bar/theme_class.h"
#include "core/widgets/tooltip.h"
#include "widgets/bluetooth/bluetooth_menu.h"
#include "widgets/menu_support/label_parts.h"
#include "widgets/system_widget_helpers.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

// One backend per runtime, shared by every Bluetooth widget (BluetoothManager.acquire/release). It
// is destroyed with the last widget. Guarded for the first-use race only; widgets live on the GUI
// thread.
std::shared_ptr<platform::BluetoothBackend> acquireBackend() {
  static std::mutex mutex;
  static std::weak_ptr<platform::BluetoothBackend> shared;
  const std::lock_guard<std::mutex> lock(mutex);
  if (auto existing = shared.lock()) return existing;
  std::shared_ptr<platform::BluetoothBackend> created(platform::createBluetoothBackend().release());
  shared = created;
  return created;
}

// len(str) / str[:n] count code points, not UTF-16 units.
QString truncated(const QString& text, int maxLength, const QString& ellipsis) {
  const QList<uint> points = text.toUcs4();
  if (maxLength <= 0 || points.size() <= maxLength) return text;
  return QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()), maxLength) +
         ellipsis;
}

}  // namespace

BluetoothWidget::BluetoothWidget(const WidgetContext& context,
                                 std::shared_ptr<platform::BluetoothBackend> backend)
    : BaseWidget(qs(context.name), context.eventBus,
                 detail::className(std::get<config::options::BluetoothConfig>(context.typedOptions),
                                   "bluetooth-widget "),
                 0),
      options_(std::get<config::options::BluetoothConfig>(context.typedOptions)),
      globalState_(context.globalState),
      backend_(backend ? std::move(backend) : acquireBackend()),
      icon_(qs(options_.icons.bluetooth_off)) {
  menu_ = new BluetoothMenu(this, options_.menu_config, backend_);

  connect(backend_.get(), &platform::BluetoothBackend::statusUpdated, this,
          &BluetoothWidget::applyStatus);
  connect(backend_.get(), &platform::BluetoothBackend::refreshFailed, this,
          [this](const QString& message) { menu_->onRefreshFailed(message); });
  connect(backend_.get(), &platform::BluetoothBackend::scanStarted, this,
          [this] { menu_->onScanStarted(); });
  connect(backend_.get(), &platform::BluetoothBackend::scanCompleted, this,
          [this](int status, const QVector<platform::BluetoothDevice>& devices) {
            menu_->onScanCompleted(status, devices);
          });
  connect(backend_.get(), &platform::BluetoothBackend::connectionFinished, this,
          [this](bool success, const QString& message, const platform::BluetoothDevice& device) {
            menu_->onConnectionFinished(success, message, device);
          });

  buildWidgetLabel(qs(options_.label), qs(options_.label_alt));
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"),
                   [this](const QStringList&) { menu_->showMenu(); });
  setCallbackBinding({qs(options_.callbacks.on_left), qs(options_.callbacks.on_middle),
                      qs(options_.callbacks.on_right), QStringLiteral("do_nothing")});

  updateLabel(qs(options_.icons.bluetooth_off), std::nullopt);
  backend_->start();
}

BluetoothWidget::~BluetoothWidget() {
  menu_->detach();
  disconnect(backend_.get(), nullptr, this, nullptr);
}

void BluetoothWidget::toggleLabel() {
  toggleAlt();
  updateLabel(icon_, connectedDevices_);
}

void BluetoothWidget::applyStatus(const platform::BluetoothStatus& status) {
  if (!status.radioOn) {
    icon_ = qs(options_.icons.bluetooth_off);
    connectedDevices_.reset();
    state_ = QStringLiteral("bt-off");
  } else {
    QStringList connected;
    for (const platform::BluetoothDevice& device : status.connectedDevices())
      connected << device.name;
    if (!connected.isEmpty()) {
      icon_ = qs(options_.icons.bluetooth_connected);
      connectedDevices_ = connected;
      state_ = QStringLiteral("bt-connected");
    } else {
      icon_ = qs(options_.icons.bluetooth_on);
      connectedDevices_.reset();
      state_ = QStringLiteral("bt-on");
    }
  }
  updateLabel(icon_, connectedDevices_);
  menu_->applyStatus(status);
}

void BluetoothWidget::setStateClass(QLabel* label) {
  QStringList classes;
  for (const QString& css : label->property("class").toString().split(
           QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
    if (css != QLatin1String("bt-off") && css != QLatin1String("bt-on") &&
        css != QLatin1String("bt-connected")) {
      classes << css;
    }
  }
  classes << state_;
  setLabelClass(label, classes.join(QLatin1Char(' ')));
}

void BluetoothWidget::updateLabel(const QString& icon,
                                  const std::optional<QStringList>& connectedDevices) {
  const bool alt = showAlt();
  const std::vector<QLabel*>& active = alt ? labelsAlt() : labels();
  const QString content = qs(alt ? options_.label_alt : options_.label);

  QString deviceNames;
  QString tooltipText;
  int count = 0;
  if (connectedDevices && !connectedDevices->isEmpty()) {
    QStringList names = *connectedDevices;
    for (QString& name : names) {
      for (const auto& alias : options_.device_aliases) {
        if (qs(alias.name).trimmed() == name.trimmed()) {
          name = qs(alias.alias);
          break;
        }
      }
    }
    count = static_cast<int>(names.size());
    deviceNames = names.join(qs(options_.label_device_separator));
    tooltipText = QStringLiteral("Connected devices");
    for (const QString& name : names) tooltipText += QStringLiteral("\n• ") + name;
  } else {
    deviceNames = qs(options_.label_no_device);
    tooltipText = deviceNames;
  }

  const std::vector<std::pair<QString, QString>> replacements = {
      {QStringLiteral("{icon}"), icon},
      {QStringLiteral("{device_name}"), deviceNames},
      {QStringLiteral("{device_count}"), QString::number(count)}};
  size_t index = 0;
  for (const QString& part : rawLabelParts(content)) {
    QString text = replacePlaceholders(part, replacements);
    const bool isSpan =
        part.contains(QLatin1String("<span")) && part.contains(QLatin1String("</span>"));
    if (!isSpan && options_.max_length && *options_.max_length > 0)
      text = truncated(text, *options_.max_length, qs(options_.max_length_ellipsis));
    if (index < active.size()) {
      active[index]->setText(text);
      setStateClass(active[index]);
    }
    ++index;
  }
  if (options_.tooltip) setTooltip(labelContainer(), tooltipText, globalState_);
}

}  // namespace yasb::widgets

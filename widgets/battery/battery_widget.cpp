#include "battery_widget.h"

#include <QCoreApplication>
#include <QMutex>
#include <QRegularExpression>

#include "core/util/natural_time.h"
#include "core/widgets/polling_worker.h"
#include "widgets/system_widget_helpers.h"

namespace yasb::widgets {
namespace {

PollingWorker* batteryWorker(int interval) {
  static QMutex mutex;
  static QPointer<PollingWorker> worker;
  QMutexLocker lock(&mutex);
  if (!worker && interval > 0 && QCoreApplication::instance()) {
    auto source = platform::createBatteryDataSource();
    worker = new PollingWorker(
        [source] {
          const auto sample = source->sample();
          return sample ? QVariant::fromValue(*sample) : QVariant{};
        },
        interval, QCoreApplication::instance());
  }
  return worker;
}

QString optionalString(const std::optional<std::string>& value) {
  return !value || value->empty() ? QStringLiteral("N/A") : QString::fromStdString(*value);
}

}  // namespace

BatteryWidget::BatteryWidget(const WidgetContext& context, bool startPolling)
    : BaseWidget(QString::fromStdString(context.name),
                 detail::className(context.options, "battery-widget ").trimmed() + " ", 0),
      options_(context.options),
      label_(detail::stringOption(options_, "label")),
      labelAlt_(detail::stringOption(options_, "label_alt")),
      hideUnsupported_(options_.value("hide_unsupported", true)),
      naturalTime_(options_.value("time_remaining_natural", false)) {
  buildWidgetLabel(label_, labelAlt_);
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  setCallbackBinding({detail::callbackOption(options_, "on_left"),
                      detail::callbackOption(options_, "on_middle"),
                      detail::callbackOption(options_, "on_right"), QStringLiteral("do_nothing")});
  applySample(std::nullopt);
  if (startPolling) {
    if (PollingWorker* worker = batteryWorker(options_.value("update_interval", 5000))) {
      connect(worker, &PollingWorker::dataReady, this, [this](const QVariant& value) {
        if (value.canConvert<platform::BatterySample>())
          applySample(value.value<platform::BatterySample>());
        else
          applySample(std::nullopt);
      });
      worker->start();
    }
  }
}

void BatteryWidget::applySample(const std::optional<platform::BatterySample>& sample) {
  last_ = sample;
  if (!sample && hideUnsupported_) {
    hide();
    return;
  }
  if (parentWidget()) show();
  render();
}

bool BatteryWidget::event(QEvent* event) {
  const bool handled = BaseWidget::event(event);
  if (event->type() == QEvent::ParentChange && parentWidget() && (last_ || !hideUnsupported_))
    show();
  return handled;
}

QString BatteryWidget::threshold(int percent) const {
  const auto t = detail::objectOption(options_, "status_thresholds");
  if (percent <= t.value("critical", 10)) return QStringLiteral("critical");
  if (percent <= t.value("low", 25)) return QStringLiteral("low");
  if (percent <= t.value("medium", 75)) return QStringLiteral("medium");
  if (percent <= t.value("high", 95)) return QStringLiteral("high");
  return QStringLiteral("full");
}

QString BatteryWidget::timeRemaining(int seconds) const {
  if (seconds == -2)
    return detail::stringOption(options_, "time_remaining_unlimited_icon", "unlimited");
  if (seconds < 0) return QStringLiteral("unknown");
  if (naturalTime_) return QString::fromStdString(util::naturalTimePhrase(seconds));
  return QString::fromStdString(timedeltaStr(seconds));
}

QString BatteryWidget::chargingIcon(const QString& state) const {
  const auto icons = detail::objectOption(options_, "status_icons");
  const QString icon =
      QString::fromStdString(icons.value("icon_" + state.toStdString(), std::string()));
  if (!last_ || (!last_->charging && !last_->plugged)) return icon;
  const auto charging = detail::objectOption(options_, "charging_options");
  const QString format = QString::fromStdString(charging.value("icon_format", std::string()));
  try {
    return QString::fromStdString(pyFormat(
        format.toStdString(), {{"charging_icon", icons.value("icon_charging", std::string())},
                               {"icon", icon.toStdString()}}));
  } catch (const FormatError&) {
    return icon;
  }
}

void BatteryWidget::render() {
  const bool alt = showAlt();
  const auto& active = alt ? labelsAlt() : labels();
  const auto parts = splitLabelParts(alt ? labelAlt_ : label_, alt);
  if (!last_) {
    for (size_t i = 0; i < parts.size() && i < active.size(); ++i) {
      if (parts[i].isIcon) active[i]->hide();
      active[i]->setText(QStringLiteral("Battery info not available"));
    }
    return;
  }

  const auto& state = *last_;
  const QString original = threshold(state.percent);
  const QString status = state.charging ? QStringLiteral("charging") : original;
  const QString powerClass = state.plugged ? QString() : QStringLiteral("discharging");
  const std::vector<std::pair<QString, QString>> replacements = {
      {"{percent}", QString::number(state.percent)},
      {"{time_remaining}", timeRemaining(state.timeRemaining)},
      {"{is_charging}", state.charging ? "yes" : "no"},
      {"{icon}", chargingIcon(original)},
      {"{power}", detail::optionalText(state.rate, 1, true)},
      {"{voltage}", detail::optionalText(state.voltage, 2)},
      {"{capacity}", detail::optionalText(state.capacity)},
      {"{full_capacity}", detail::optionalText(state.fullCapacity)},
      {"{designed_capacity}", detail::optionalText(state.designedCapacity)},
      {"{temperature}", detail::optionalText(state.temperature, 1)},
      {"{cycle_count}", detail::optionalText(state.cycleCount)},
      {"{health}", detail::optionalText(state.healthPercent, 1)},
      {"{chemistry}", optionalString(state.chemistry)}};
  for (size_t i = 0; i < parts.size() && i < active.size(); ++i) {
    const QString text = replacePlaceholders(parts[i].text, replacements);
    if (parts[i].isIcon) {
      active[i]->setText(text);
      QString css = active[i]->property("class").toString();
      css.remove(QRegularExpression(QStringLiteral("status-\\w+|\\bdischarging\\b")));
      BaseWidget::setLabelClass(active[i],
                                (css + " status-" + status + " " + powerClass).simplified());
    } else {
      QString rendered = text;
      try {
        rendered = QString::fromStdString(pyFormat(text.toStdString(), nlohmann::json::object()));
      } catch (const FormatError&) {
      }
      active[i]->setText(rendered);
      BaseWidget::setLabelClass(active[i], (QStringLiteral("label ") + (alt ? "alt " : "") +
                                            "status-" + status + " " + powerClass)
                                               .simplified());
    }
  }
}

void BatteryWidget::toggleLabel() {
  toggleAlt();
  render();
}

}  // namespace yasb::widgets

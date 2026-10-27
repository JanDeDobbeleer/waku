#include "widgets/battery/battery_widget.h"

#include <QEvent>
#include <QRegularExpression>

#include "core/util/natural_time.h"
#include "core/widgets/polling_worker_registry.h"
#include "widgets/system_widget_helpers.h"

namespace yasb::widgets {
namespace {

QString optionalString(const std::optional<std::string>& value) {
  return !value || value->empty() ? QStringLiteral("N/A") : QString::fromStdString(*value);
}

}  // namespace

BatteryWidget::BatteryWidget(const WidgetContext& context, bool startPolling)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 detail::className(std::get<config::options::BatteryConfig>(context.typedOptions),
                                   "battery-widget ")
                         .trimmed() +
                     " ",
                 0),
      options_(std::get<config::options::BatteryConfig>(context.typedOptions)),
      label_(QString::fromStdString(options_.label)),
      labelAlt_(QString::fromStdString(options_.label_alt)),
      hideUnsupported_(options_.hide_unsupported),
      naturalTime_(options_.time_remaining_natural) {
  buildWidgetLabel(label_, labelAlt_);
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("do_nothing")});
  applySample(std::nullopt);
  if (startPolling) {
    const int interval = options_.update_interval;
    if (PollingWorker* worker = context.pollingWorkers.get("battery", interval, [] {
          auto source = platform::createBatteryDataSource();
          return [source] {
            const auto sample = source->sample();
            return sample ? QVariant::fromValue(*sample) : QVariant{};
          };
        })) {
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
  const auto& t = options_.status_thresholds;
  if (percent <= t.critical) return QStringLiteral("critical");
  if (percent <= t.low) return QStringLiteral("low");
  if (percent <= t.medium) return QStringLiteral("medium");
  if (percent <= t.high) return QStringLiteral("high");
  return QStringLiteral("full");
}

QString BatteryWidget::timeRemaining(int seconds) const {
  if (seconds == -2) return QString::fromStdString(options_.time_remaining_unlimited_icon);
  if (seconds < 0) return QStringLiteral("unknown");
  if (naturalTime_) return QString::fromStdString(util::naturalTimePhrase(seconds));
  return QString::fromStdString(timedeltaStr(seconds));
}

QString BatteryWidget::chargingIcon(const QString& state) const {
  const auto& icons = options_.status_icons;
  const std::string icon = state == "critical" ? icons.icon_critical
                           : state == "low"    ? icons.icon_low
                           : state == "medium" ? icons.icon_medium
                           : state == "high"   ? icons.icon_high
                           : state == "full"   ? icons.icon_full
                                               : std::string();
  if (!last_ || (!last_->charging && !last_->plugged)) return QString::fromStdString(icon);
  const QString format = QString::fromStdString(options_.charging_options.icon_format);
  try {
    return QString::fromStdString(
        pyFormat(format.toStdString(), {{"charging_icon", icons.icon_charging}, {"icon", icon}}));
  } catch (const FormatError&) {
    return QString::fromStdString(icon);
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

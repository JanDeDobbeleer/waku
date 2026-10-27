#include "widgets/memory/memory_widget.h"

#include <QCoreApplication>
#include <QLabel>
#include <QMutex>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "core/widgets/polling_worker.h"
#include "widgets/system_widget_helpers.h"

namespace yasb::widgets {
namespace {

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

PollingWorker* memoryWorker(int interval) {
  static QMutex mutex;
  static QPointer<PollingWorker> worker;
  QMutexLocker lock(&mutex);
  if (!worker && interval > 0 && QCoreApplication::instance()) {
    auto source = platform::createMemoryDataSource();
    worker = new PollingWorker([source] { return QVariant::fromValue(source->sample()); }, interval,
                               QCoreApplication::instance());
  }
  return worker;
}

QString popupSize(std::uint64_t bytes) {
  static const char* units[] = {"Bytes", "KB", "MB", "GB", "TB", "PB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit < 5) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0) return bytes == 1 ? QStringLiteral("1 Byte") : QString::number(bytes) + " Bytes";
  return QString::number(value, 'f', 1) + " " + units[unit];
}

QString labelSize(std::uint64_t bytes, bool hideDecimal) {
  return QString::fromStdString(naturalSizeGnu(static_cast<double>(bytes), hideDecimal ? 0 : 1));
}

QString histogram(double value, double minimum, double maximum, const nlohmann::json& options) {
  const auto configured = options.value("histogram_icons", std::vector<std::string>{});
  const std::vector<std::string> icons =
      configured.empty() ? std::vector<std::string>{"▁", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"}
                         : configured;
  if (maximum == minimum) return QString::fromStdString(icons.front());
  const double raw =
      (value - minimum) / (maximum - minimum) * static_cast<double>(icons.size() - 1);
  const auto index =
      static_cast<size_t>(std::clamp(static_cast<int>(raw), 0, static_cast<int>(icons.size() - 1)));
  return QString::fromStdString(icons[index]);
}

}  // namespace

MemoryWidget::MemoryWidget(const WidgetContext& context, bool startPolling)
    : BaseWidget(QString::fromStdString(context.name),
                 detail::className(context.options, "memory-widget ").trimmed() + " ", 0),
      options_(context.options),
      label_(detail::stringOption(options_, "label")),
      labelAlt_(detail::stringOption(options_, "label_alt")),
      hideDecimal_(options_.value("hide_decimal", false)),
      historyLimit_(detail::objectOption(options_, "menu").value("graph_history_size", 60)) {
  buildWidgetLabel(label_, labelAlt_);
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"), [this](const QStringList&) { toggleMenu(); });
  setCallbackBinding({detail::callbackOption(options_, "on_left"), QStringLiteral("do_nothing"),
                      detail::callbackOption(options_, "on_right"), QStringLiteral("do_nothing")});
  last_ = platform::MemorySample{};
  render();
  if (startPolling) {
    if (PollingWorker* worker = memoryWorker(options_.value("update_interval", 5000))) {
      connect(worker, &PollingWorker::dataReady, this, [this](const QVariant& value) {
        applySample(value.value<platform::MemorySample>());
      });
      worker->start();
    }
  }
}

void MemoryWidget::applySample(const platform::MemorySample& sample) {
  last_ = sample;
  if (detail::objectOption(options_, "menu").value("enabled", false)) {
    detail::appendHistory(graphHistory_, sample.percent, historyLimit_);
  }
  render();
  updatePopup();
}

QString MemoryWidget::threshold(double value) const {
  return detail::thresholdClass(value, detail::objectOption(options_, "memory_thresholds"),
                                "status-");
}

void MemoryWidget::render() {
  if (!last_) return;
  const auto& sampleData = *last_;
  const QString percent = detail::jsonNumber(sampleData.percent, hideDecimal_);
  const QString swapPercent = detail::jsonNumber(sampleData.swapPercent, hideDecimal_);
  const QString hist = histogram(sampleData.percent, 0, 100, options_);
  const std::vector<std::pair<QString, QString>> pairs = {
      {"{virtual_mem_free}", labelSize(sampleData.free, hideDecimal_)},
      {"{virtual_mem_percent}", percent},
      {"{virtual_mem_total}", labelSize(sampleData.total, hideDecimal_)},
      {"{virtual_mem_avail}", labelSize(sampleData.available, hideDecimal_)},
      {"{virtual_mem_used}", labelSize(sampleData.used, hideDecimal_)},
      {"{virtual_mem_outof}", labelSize(sampleData.used, hideDecimal_) + " / " +
                                  labelSize(sampleData.total, hideDecimal_)},
      {"{swap_mem_free}", labelSize(sampleData.swapFree, hideDecimal_)},
      {"{swap_mem_percent}", swapPercent},
      {"{swap_mem_total}", labelSize(sampleData.swapTotal, hideDecimal_)},
      {"{histogram}", hist}};
  const QString activeTemplate = showAlt() ? labelAlt_ : label_;
  const auto& active = showAlt() ? labelsAlt() : labels();
  const auto parts = splitLabelParts(activeTemplate, showAlt());
  for (size_t i = 0; i < parts.size() && i < active.size(); ++i) {
    if (parts[i].isIcon) {
      active[i]->setText(parts[i].text);
      continue;
    }
    active[i]->setText(replacePlaceholders(parts[i].text, pairs));
    setLabelClass(active[i], (showAlt() ? QStringLiteral("label alt ") : QStringLiteral("label ")) +
                                 threshold(sampleData.percent));
  }
}

void MemoryWidget::toggleLabel() {
  toggleAlt();
  render();
}

void MemoryWidget::toggleMenu() {
  if (!detail::objectOption(options_, "menu").value("enabled", false)) return;
  if (popup_ && popup_->isVisible()) {
    popup_->hide();
    return;
  }
  openPopup();
}

void MemoryWidget::openPopup() {
  const auto& sampleData = *last_;
  auto menu = detail::popupConfig(options_, false);
  const std::vector<StatRow> rows = {
      {QStringLiteral("In use"), "used", popupSize(sampleData.used), QStringLiteral("All memory"),
       "total", popupSize(sampleData.total)},
      {QStringLiteral("Cached"), "cached", popupSize(sampleData.cached),
       QStringLiteral("Available"), "avail", popupSize(sampleData.available)},
      {QStringLiteral("Swap used"), "swap", popupSize(sampleData.swapUsed),
       QStringLiteral("Utilization"), "util", QString::number(pyRound(sampleData.percent)) + "%"}};
  popup_ = buildStatPopup(this, menu, QStringLiteral("memory-popup"),
                          QStringLiteral("<b>Memory</b> Usage"), detail::asVector(graphHistory_),
                          rows, QStringLiteral("memory-graph"));
  if (menu.showGraph && popup_->graph) {
    auto* layout = qobject_cast<QVBoxLayout*>(popup_->layout());
    auto* graphContainer = popup_->graph->parentWidget();
    if (layout && graphContainer) {
      auto* title = new QLabel(QStringLiteral("Utilization"));
      title->setProperty("class", "graph-title");
      layout->insertWidget(layout->indexOf(graphContainer), title);
    }
  }
  popup_->show();
}

void MemoryWidget::updatePopup() {
  if (!popup_ || !popup_->isVisible() || !last_) return;
  const auto& sampleData = *last_;
  if (popup_->graph) popup_->graph->setData(detail::asVector(graphHistory_));
  const std::pair<const char*, QString> values[] = {
      {"used", popupSize(sampleData.used)},
      {"total", popupSize(sampleData.total)},
      {"cached", popupSize(sampleData.cached)},
      {"avail", popupSize(sampleData.available)},
      {"swap", popupSize(sampleData.swapUsed)},
      {"util", QString::number(pyRound(sampleData.percent)) + "%"}};
  for (const auto& [key, value] : values) {
    if (auto* label = popup_->statLabels.value(QString::fromLatin1(key))) label->setText(value);
  }
}

}  // namespace yasb::widgets

#include "widgets/cpu/cpu_widget.h"

#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "core/widgets/polling_worker_registry.h"
#include "widgets/system_widget_helpers.h"

namespace yasb::widgets {
namespace {

nlohmann::json jsonValue(double value, bool hideDecimal) {
  return hideDecimal ? nlohmann::json(pyRound(value)) : nlohmann::json(value);
}

}  // namespace

CpuWidget::CpuWidget(const WidgetContext& context, bool startPolling)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 detail::className(std::get<config::options::CpuConfig>(context.typedOptions),
                                   "cpu-widget ")
                         .trimmed() +
                     " ",
                 0),
      options_(std::get<config::options::CpuConfig>(context.typedOptions)),
      globalState_(context.globalState),
      label_(QString::fromStdString(options_.label)),
      labelAlt_(QString::fromStdString(options_.label_alt)),
      hideDecimal_(options_.hide_decimal),
      historyLimit_(options_.menu.graph_history_size) {
  buildWidgetLabel(label_, labelAlt_);
  const int columns = std::max(1, options_.histogram_num_columns);
  frequencyHistory_.assign(static_cast<size_t>(columns), 0);
  percentHistory_.assign(static_cast<size_t>(columns), 0);
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"), [this](const QStringList&) { toggleMenu(); });
  setCallbackBinding(
      {QString::fromStdString(options_.callbacks.on_left), QStringLiteral("do_nothing"),
       QString::fromStdString(options_.callbacks.on_right), QStringLiteral("do_nothing")});

  platform::CpuSample initial;
  initial.percentPerCore = {0};
  initial.coresPhysical = 1;
  initial.coresLogical = 1;
  last_ = initial;
  render();

  if (startPolling) {
    const int interval = options_.update_interval;
    if (PollingWorker* worker = context.pollingWorkers.get("cpu", interval, [] {
          auto source = platform::createCpuDataSource();
          return [source] { return QVariant::fromValue(source->sample()); };
        })) {
      connect(worker, &PollingWorker::dataReady, this,
              [this](const QVariant& value) { applySample(value.value<platform::CpuSample>()); });
      worker->start();
    }
  }
}

void CpuWidget::applySample(const platform::CpuSample& sample) {
  last_ = sample;
  if (options_.menu.enabled) {
    detail::appendHistory(graphHistory_, sample.percent, historyLimit_);
  }
  render();
  updatePopup();
}

QString CpuWidget::threshold(double value) const {
  return detail::thresholdClass(value, options_.cpu_thresholds, "status-");
}

QString CpuWidget::histogram(double value, double minimum, double maximum) const {
  const auto& icons = options_.histogram_icons;
  if (maximum == minimum) return QString::fromStdString(icons.front());
  const double raw =
      (value - minimum) / (maximum - minimum) * static_cast<double>(icons.size() - 1);
  const auto index =
      static_cast<size_t>(std::clamp(static_cast<int>(raw), 0, static_cast<int>(icons.size() - 1)));
  return QString::fromStdString(icons[index]);
}

void CpuWidget::render() {
  if (!last_) return;
  const auto& sampleData = *last_;
  detail::appendHistory(frequencyHistory_, sampleData.frequencyCurrent,
                        static_cast<int>(frequencyHistory_.size()));
  detail::appendHistory(percentHistory_, sampleData.percent,
                        static_cast<int>(percentHistory_.size()));

  nlohmann::json perCore = nlohmann::json::array();
  for (const double value : sampleData.percentPerCore)
    perCore.push_back(jsonValue(value, hideDecimal_));
  std::string freqHistory;
  std::string cpuHistory;
  std::string coresHistory;
  for (const double value : frequencyHistory_)
    freqHistory += histogram(value, sampleData.frequencyMin, sampleData.frequencyMax).toStdString();
  for (const double value : percentHistory_) cpuHistory += histogram(value, 0, 100).toStdString();
  for (const double value : sampleData.percentPerCore)
    coresHistory += histogram(value, 0, 100).toStdString();
  nlohmann::json info = {
      {"cores", {{"physical", sampleData.coresPhysical}, {"total", sampleData.coresLogical}}},
      {"freq",
       {{"min", jsonValue(sampleData.frequencyMin, hideDecimal_)},
        {"max", jsonValue(sampleData.frequencyMax, hideDecimal_)},
        {"current", jsonValue(sampleData.frequencyCurrent, hideDecimal_)}}},
      {"percent", {{"core", perCore}, {"total", jsonValue(sampleData.percent, hideDecimal_)}}},
      {"stats",
       {{"context_switches", 0}, {"interrupts", 0}, {"soft_interrupts", 0}, {"sys_calls", 0}}},
      {"histograms",
       {{"cpu_freq", freqHistory}, {"cpu_percent", cpuHistory}, {"cores", coresHistory}}}};

  const QString activeTemplate = showAlt() ? labelAlt_ : label_;
  const auto& active = showAlt() ? labelsAlt() : labels();
  const auto parts = splitLabelParts(activeTemplate, showAlt());
  for (size_t i = 0; i < parts.size() && i < active.size(); ++i) {
    if (parts[i].isIcon) {
      active[i]->setText(parts[i].text);
      continue;
    }
    try {
      active[i]->setText(
          QString::fromStdString(pyFormat(parts[i].text.toStdString(), {{"info", info}})));
    } catch (const FormatError& error) {
      qWarning().noquote() << "CPU label format failed:" << error.what();
    }
    setLabelClass(active[i], (showAlt() ? QStringLiteral("label alt ") : QStringLiteral("label ")) +
                                 threshold(sampleData.percent));
  }
}

void CpuWidget::toggleLabel() {
  toggleAlt();
  render();
}

void CpuWidget::toggleMenu() {
  if (!options_.menu.enabled) return;
  if (popup_ && popup_->isVisible()) {
    popup_->hide();
    return;
  }
  openPopup();
}

void CpuWidget::openPopup() {
  const auto& sampleData = *last_;
  auto menu = detail::popupConfig(options_.menu);
  std::vector<StatRow> rows = {
      {QStringLiteral("Usage"), QStringLiteral("usage"),
       QString::number(pyRound(sampleData.percent)) + "%", QStringLiteral("Frequency"),
       QStringLiteral("freq"), QString::number(pyRound(sampleData.frequencyCurrent)) + " MHz"},
      {QStringLiteral("Cores (P / L)"), QStringLiteral("cores"),
       QString::number(sampleData.coresPhysical) + " / " + QString::number(sampleData.coresLogical),
       QStringLiteral("Max frequency"), QStringLiteral("max_freq"),
       QString::number(pyRound(sampleData.frequencyMax)) + " MHz"}};
  popup_ = buildStatPopup(this, globalState_, menu, QStringLiteral("cpu-popup"),
                          QStringLiteral("<b>CPU</b> Usage"), detail::asVector(graphHistory_), rows,
                          QStringLiteral("cpu-graph"));
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

void CpuWidget::updatePopup() {
  if (!popup_ || !popup_->isVisible() || !last_) return;
  if (popup_->graph) popup_->graph->setData(detail::asVector(graphHistory_));
  const auto& sampleData = *last_;
  if (auto* label = popup_->statLabels.value("usage"))
    label->setText(QString::number(pyRound(sampleData.percent)) + "%");
  if (auto* label = popup_->statLabels.value("freq"))
    label->setText(QString::number(pyRound(sampleData.frequencyCurrent)) + " MHz");
  if (auto* label = popup_->statLabels.value("cores")) {
    label->setText(QString::number(sampleData.coresPhysical) + " / " +
                   QString::number(sampleData.coresLogical));
  }
  if (auto* label = popup_->statLabels.value("max_freq")) {
    label->setText(QString::number(pyRound(sampleData.frequencyMax)) + " MHz");
  }
}

}  // namespace yasb::widgets

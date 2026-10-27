#include "cpu_widget.h"

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

PollingWorker* cpuWorker(int interval) {
  static QMutex mutex;
  static QPointer<PollingWorker> worker;
  QMutexLocker lock(&mutex);
  if (!worker && interval > 0 && QCoreApplication::instance()) {
    auto source = platform::createCpuDataSource();
    worker = new PollingWorker([source] { return QVariant::fromValue(source->sample()); }, interval,
                               QCoreApplication::instance());
  }
  return worker;
}

nlohmann::json jsonValue(double value, bool hideDecimal) {
  return hideDecimal ? nlohmann::json(pyRound(value)) : nlohmann::json(value);
}

}  // namespace

CpuWidget::CpuWidget(const WidgetContext& context, bool startPolling)
    : BaseWidget(QString::fromStdString(context.name),
                 detail::className(context.options, "cpu-widget ").trimmed() + " ", 0),
      options_(context.options),
      label_(detail::stringOption(options_, "label")),
      labelAlt_(detail::stringOption(options_, "label_alt")),
      hideDecimal_(options_.value("hide_decimal", false)),
      historyLimit_(detail::objectOption(options_, "menu").value("graph_history_size", 60)) {
  buildWidgetLabel(label_, labelAlt_);
  const int columns = std::max(1, options_.value("histogram_num_columns", 10));
  frequencyHistory_.assign(static_cast<size_t>(columns), 0);
  percentHistory_.assign(static_cast<size_t>(columns), 0);
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"), [this](const QStringList&) { toggleMenu(); });
  setCallbackBinding({detail::callbackOption(options_, "on_left"), QStringLiteral("do_nothing"),
                      detail::callbackOption(options_, "on_right"), QStringLiteral("do_nothing")});

  platform::CpuSample initial;
  initial.percentPerCore = {0};
  initial.coresPhysical = 1;
  initial.coresLogical = 1;
  last_ = initial;
  render();

  if (startPolling) {
    if (PollingWorker* worker = cpuWorker(options_.value("update_interval", 1000))) {
      connect(worker, &PollingWorker::dataReady, this,
              [this](const QVariant& value) { applySample(value.value<platform::CpuSample>()); });
      worker->start();
    }
  }
}

void CpuWidget::applySample(const platform::CpuSample& sample) {
  last_ = sample;
  if (detail::objectOption(options_, "menu").value("enabled", false)) {
    detail::appendHistory(graphHistory_, sample.percent, historyLimit_);
  }
  render();
  updatePopup();
}

QString CpuWidget::threshold(double value) const {
  return detail::thresholdClass(value, detail::objectOption(options_, "cpu_thresholds"), "status-");
}

QString CpuWidget::histogram(double value, double minimum, double maximum) const {
  const auto configured = options_.value("histogram_icons", std::vector<std::string>{});
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
  if (!detail::objectOption(options_, "menu").value("enabled", false)) return;
  if (popup_ && popup_->isVisible()) {
    popup_->hide();
    return;
  }
  openPopup();
}

void CpuWidget::openPopup() {
  const auto& sampleData = *last_;
  auto menu = detail::popupConfig(options_, false);
  std::vector<StatRow> rows = {
      {QStringLiteral("Usage"), QStringLiteral("usage"),
       QString::number(pyRound(sampleData.percent)) + "%", QStringLiteral("Frequency"),
       QStringLiteral("freq"), QString::number(pyRound(sampleData.frequencyCurrent)) + " MHz"},
      {QStringLiteral("Cores (P / L)"), QStringLiteral("cores"),
       QString::number(sampleData.coresPhysical) + " / " + QString::number(sampleData.coresLogical),
       QStringLiteral("Max frequency"), QStringLiteral("max_freq"),
       QString::number(pyRound(sampleData.frequencyMax)) + " MHz"}};
  popup_ =
      buildStatPopup(this, menu, QStringLiteral("cpu-popup"), QStringLiteral("<b>CPU</b> Usage"),
                     detail::asVector(graphHistory_), rows, QStringLiteral("cpu-graph"));
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

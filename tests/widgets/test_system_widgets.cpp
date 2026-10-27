#include <gtest/gtest.h>

#include <QApplication>
#include <memory>

#include "core/widgets/registry.h"
#include "core/widgets/stat_popup.h"
#include "widgets/battery/battery_widget.h"
#include "widgets/cpu/cpu_widget.h"
#include "widgets/memory/memory_widget.h"
#include "widgets/stat_widgets.h"

using nlohmann::json;
using namespace yasb::widgets;
using namespace yasb::platform;

namespace {

std::unique_ptr<QApplication> g_app;

WidgetContext context(const char* widget, const char* type, json options) {
  return {widget, type, std::move(options), {}, {}};
}

json cpuOptions(bool menu = false) {
  return {{"label", "<span>\ueea1</span> {info[percent][total]}%"},
          {"label_alt", "<span>\ueea1</span> {info[freq][current]} MHz"},
          {"update_interval", 2000},
          {"hide_decimal", true},
          {"menu",
           {{"enabled", menu},
            {"blur", false},
            {"show_graph", true},
            {"show_graph_grid", true},
            {"graph_history_size", 2}}}};
}

json memoryOptions() {
  return {{"label", "<span>\ueea0</span> {virtual_mem_percent}%"},
          {"label_alt", "<span>\ueea0</span> {virtual_mem_outof}"},
          {"update_interval", 5000},
          {"hide_decimal", true},
          {"menu",
           {{"enabled", true},
            {"blur", false},
            {"show_graph", true},
            {"show_graph_grid", true},
            {"graph_history_size", 2}}}};
}

json batteryOptions(bool hideUnsupported = true) {
  return {
      {"label", "<span>{icon}</span> {percent}%"},
      {"label_alt", "<span>{icon}</span> {percent}% | {time_remaining}"},
      {"update_interval", 5000},
      {"time_remaining_natural", true},
      {"hide_unsupported", hideUnsupported},
      {"charging_options", {{"icon_format", "{charging_icon}"}, {"blink_charging_icon", false}}},
      {"status_thresholds",
       {{"critical", 10}, {"low", 25}, {"medium", 75}, {"high", 95}, {"full", 100}}},
      {"status_icons",
       {{"icon_charging", "\uea93"},
        {"icon_critical", "\ue850"},
        {"icon_low", "\ue852"},
        {"icon_medium", "\ue855"},
        {"icon_high", "\ue858"},
        {"icon_full", "\ue83f"}}}};
}

}  // namespace

class SystemWidgetTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    int argc = 1;
    static char name[] = "system-widget-tests";
    static char* argv[] = {name, nullptr};
    g_app = std::make_unique<QApplication>(argc, argv);
  }
  static void TearDownTestSuite() { g_app.reset(); }
};

TEST_F(SystemWidgetTest, CpuUsesConfiguredLabelAndStatusClass) {
  CpuWidget widget(context("cpu", "yasb.cpu.CpuWidget", cpuOptions()), false);
  widget.show();
  CpuSample sample;
  sample.percent = 37.4;
  sample.percentPerCore = {37.4};
  sample.frequencyCurrent = 1800;
  sample.coresPhysical = 4;
  sample.coresLogical = 8;
  widget.applySample(sample);

  ASSERT_EQ(widget.labels().size(), 2u);
  EXPECT_EQ(widget.labels()[1]->text(), "37%");
  EXPECT_EQ(widget.labels()[1]->property("class").toString(), "label status-medium");
  ASSERT_EQ(widget.labelsAlt().size(), 2u);
  widget.runCallback("toggle_label");
  EXPECT_EQ(widget.labelsAlt()[1]->text(), "1800 MHz");
  EXPECT_EQ(widget.labelsAlt()[1]->property("class").toString(), "label alt status-medium");
}

TEST_F(SystemWidgetTest, MemoryFormatsUsageAndOutOfLabel) {
  MemoryWidget widget(context("memory", "yasb.memory.MemoryWidget", memoryOptions()), false);
  widget.show();
  MemorySample sample;
  sample.total = static_cast<std::uint64_t>(31.6 * 1024 * 1024 * 1024);
  sample.used = static_cast<std::uint64_t>(12.3 * 1024 * 1024 * 1024);
  sample.available = sample.total - sample.used;
  sample.free = sample.available;
  sample.percent = std::round(static_cast<double>(sample.used) / sample.total * 1000) / 10;
  widget.applySample(sample);

  EXPECT_EQ(widget.labels()[1]->text(), "39%");
  EXPECT_EQ(widget.labels()[1]->property("class").toString(), "label status-medium");
  widget.runCallback("toggle_label");
  EXPECT_EQ(widget.labelsAlt()[1]->text(), "12G / 32G");
  EXPECT_EQ(widget.labelsAlt()[1]->property("class").toString(), "label alt status-medium");
}

TEST_F(SystemWidgetTest, BatteryThresholdChargingNaturalTimeAndUnsupportedState) {
  BatteryWidget widget(context("battery", "yasb.battery.BatteryWidget", batteryOptions()), false);
  BatterySample sample;
  sample.percent = 10;
  sample.timeRemaining = 7380;
  widget.applySample(sample);
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "icon status-critical discharging");
  EXPECT_EQ(widget.labels()[0]->text(), QString::fromUtf8("\ue850"));
  EXPECT_EQ(widget.labels()[1]->property("class").toString(), "label status-critical discharging");

  sample.charging = true;
  sample.plugged = true;
  widget.applySample(sample);

  ASSERT_EQ(widget.labels().size(), 2u);
  EXPECT_EQ(widget.labels()[0]->text(), QString::fromUtf8("\uea93"));
  EXPECT_EQ(widget.labels()[0]->property("class").toString(), "icon status-charging");
  EXPECT_EQ(widget.labels()[1]->text(), "10%");
  widget.runCallback("toggle_label");
  EXPECT_EQ(widget.labelsAlt()[1]->text(), "10% | 2 hours");

  widget.applySample(std::nullopt);
  EXPECT_TRUE(widget.isHidden());

  BatteryWidget visible(
      context("battery-visible", "yasb.battery.BatteryWidget", batteryOptions(false)), false);
  visible.applySample(std::nullopt);
  visible.show();
  EXPECT_FALSE(visible.isHidden());
  EXPECT_TRUE(visible.labels()[0]->isHidden());
  EXPECT_EQ(visible.labels()[0]->text(), "Battery info not available");
}

TEST_F(SystemWidgetTest, StatPopupHasHistoryGraphAndPinControl) {
  CpuWidget widget(context("cpu", "yasb.cpu.CpuWidget", cpuOptions(true)), false);
  CpuSample sample;
  sample.percent = 37.4;
  sample.percentPerCore = {37.4};
  sample.frequencyCurrent = 1800;
  widget.applySample(sample);
  sample.percent = 42;
  widget.applySample(sample);
  sample.percent = 50.5;
  widget.applySample(sample);
  widget.runCallback("toggle_menu");

  PinnablePopup* popup = nullptr;
  for (QWidget* candidate : QApplication::topLevelWidgets()) {
    if ((popup = qobject_cast<PinnablePopup*>(candidate))) break;
  }
  ASSERT_NE(popup, nullptr);
  ASSERT_NE(popup->graph, nullptr);
  EXPECT_EQ(popup->graph->property("class").toString(), "cpu-graph");
  EXPECT_EQ(popup->statLabels.value("usage")->text(), "50%");
  popup->pinButton->setChecked(true);
  EXPECT_TRUE(popup->isPinned());
}

TEST_F(SystemWidgetTest, RegistersOnlyTheThreeStatWidgetFactories) {
  registerStatWidgets();
  const auto& registry = WidgetRegistry::instance();
  EXPECT_TRUE(registry.contains("yasb.cpu.CpuWidget"));
  EXPECT_TRUE(registry.contains("yasb.memory.MemoryWidget"));
  EXPECT_TRUE(registry.contains("yasb.battery.BatteryWidget"));
}

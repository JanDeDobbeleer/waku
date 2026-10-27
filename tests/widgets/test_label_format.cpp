#include <gtest/gtest.h>

#include <QApplication>
#include <QLayout>
#include <QRegularExpression>
#include <algorithm>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>

#include "core/widgets/base_widget.h"
#include "core/widgets/label_format.h"

using nlohmann::json;
using namespace yasb::widgets;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

class LabelFormatTest : public testing::Test {};

const std::vector<std::string> kHistogramIcons = {"\u2581", "\u2581", "\u2582", "\u2583", "\u2584",
                                                  "\u2585", "\u2586", "\u2587", "\u2588"};
constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

std::string histogramBar(double num, double lo, double hi) {
  if (hi == lo) return kHistogramIcons[0];
  const int last = static_cast<int>(kHistogramIcons.size()) - 1;
  int idx = static_cast<int>((num - lo) / (hi - lo) * last);
  idx = std::min(std::max(idx, 0), last);
  return kHistogramIcons[idx];
}

std::string pyStr(const json& v) { return pyFormat("{v}", json{{"v", v}}); }

struct Thresholds {
  double low = 25;
  double medium = 50;
  double high = 90;
  std::string classify(double p) const {
    if (p <= low) return "low";
    if (p <= medium) return "medium";
    if (p <= high) return "high";
    return "critical";
  }
};

// Test-local strategy widget: the per-widget part of cpu.py / memory.py (data -> text and status
// class) on top of the shared BaseWidget label machinery. U8's real widgets replace this.
class VectorWidget : public BaseWidget {
 public:
  VectorWidget(bool memory, const json& options, EventBus& eventBus, QWidget* host)
      : BaseWidget(memory ? "memory" : "cpu", eventBus, memory ? "memory-widget " : "cpu-widget ",
                   0, host),
        memory_(memory),
        hideDecimal_(options.value("hide_decimal", false)),
        label_(qs(options.at("label"))),
        labelAlt_(qs(options.at("label_alt"))) {
    const char* key = memory ? "memory_thresholds" : "cpu_thresholds";
    if (options.contains(key)) {
      const json& t = options.at(key);
      thresholds_ = Thresholds{t.at("low").get<double>(), t.at("medium").get<double>(),
                               t.at("high").get<double>()};
    }
    buildWidgetLabel(label_, labelAlt_);
    history_.assign(10, 0.0);
    if (memory) {
      renderMemory(MemData{});
    } else {
      renderCpu({json(0), json::array({0}), 0.0});
    }
  }

  void apply(const json& step) {
    if (step.contains("toggle")) {
      toggleAlt();
      if (!memory_ && lastCpu_)
        renderCpu(*lastCpu_);  // memory's harness has no cached data to re-render
      return;
    }
    if (!memory_) {
      CpuData d{step.at("percent"), step.value("per_core", json::array({step.at("percent")})),
                2400.0};
      lastCpu_ = d;
      renderCpu(d);
      return;
    }
    const long long total = static_cast<long long>(step.at("total_gb").get<double>() * kGiB);
    const long long used = static_cast<long long>(step.at("used_gb").get<double>() * kGiB);
    const long long swapTotal = static_cast<long long>(step.value("swap_total_gb", 4.0) * kGiB);
    const long long swapUsed = static_cast<long long>(step.value("swap_used_gb", 1.0) * kGiB);
    auto round1 = [](double x) { return static_cast<double>(pyRound(x * 10.0)) / 10.0; };
    MemData m;
    m.total = total;
    m.used = used;
    m.free = total - used;
    m.percent = step.value(
        "percent",
        total ? round1(static_cast<double>(used) / static_cast<double>(total) * 100) : 0.0);
    m.swapTotal = swapTotal;
    m.swapFree = swapTotal - swapUsed;
    m.swapPercent = step.value(
        "swap_percent",
        swapTotal ? round1(static_cast<double>(swapUsed) / static_cast<double>(swapTotal) * 100)
                  : 0.0);
    renderMemory(m);
  }

 private:
  struct CpuData {
    json percent;
    json perCore;
    double freq;
  };
  struct MemData {
    long long total = 0, used = 0, free = 0, swapTotal = 0, swapFree = 0;
    double percent = 0.0;
    double swapPercent = 0.0;
  };

  json roundIf(const json& v) const { return hideDecimal_ ? json(pyRound(v.get<double>())) : v; }

  void renderParts(const std::function<QString(QString)>& fmt, double percent) {
    const bool alt = showAlt();
    const auto& active = alt ? labelsAlt() : labels();
    size_t idx = 0;
    for (const LabelPart& part : splitLabelParts(alt ? labelAlt_ : label_, alt)) {
      if (idx >= active.size()) break;
      if (part.isIcon) {
        active[idx]->setText(part.text);
      } else {
        active[idx]->setText(fmt(part.text));
        const QString base = alt ? QStringLiteral("label alt") : QStringLiteral("label");
        setLabelClass(active[idx],
                      base + QStringLiteral(" status-") + qs(thresholds_.classify(percent)));
      }
      ++idx;
    }
  }

  void renderCpu(const CpuData& d) {
    history_.push_back(d.percent.get<double>());
    history_.pop_front();
    std::string hist;
    for (double p : history_) hist += histogramBar(p, 0, 100);
    json cores = json::array();
    for (const auto& c : d.perCore) cores.push_back(roundIf(c));
    json info = {{"percent", {{"core", cores}, {"total", roundIf(d.percent)}}},
                 {"freq", {{"current", roundIf(json(d.freq))}, {"min", 0}, {"max", 0}}},
                 {"histograms", {{"cpu_percent", hist}}}};
    renderParts([&](QString t) { return qs(pyFormat(t.toStdString(), {{"info", info}})); },
                d.percent.get<double>());
  }

  void renderMemory(const MemData& m) {
    auto size = [&](long long v) {
      return qs(naturalSizeGnu(static_cast<double>(v), hideDecimal_ ? 0 : 1));
    };
    const QString percentText = qs(pyStr(roundIf(json(m.percent))));
    const QString swapPercentText = qs(pyStr(roundIf(json(m.swapPercent))));
    const std::vector<std::pair<QString, QString>> pairs = {
        {"{virtual_mem_free}", size(m.free)},
        {"{virtual_mem_percent}", percentText},
        {"{virtual_mem_total}", size(m.total)},
        {"{virtual_mem_avail}", size(m.free)},
        {"{virtual_mem_used}", size(m.used)},
        {"{virtual_mem_outof}", size(m.used) + " / " + size(m.total)},
        {"{swap_mem_free}", size(m.swapFree)},
        {"{swap_mem_percent}", swapPercentText},
        {"{swap_mem_total}", size(m.swapTotal)},
        {"{histogram}", qs(histogramBar(m.percent, 0, 100))},
    };
    renderParts([&](QString t) { return replacePlaceholders(t, pairs); }, m.percent);
  }

  bool memory_;
  bool hideDecimal_;
  QString label_;
  QString labelAlt_;
  Thresholds thresholds_;
  std::deque<double> history_;
  std::optional<CpuData> lastCpu_;
};

// Strategy for battery.py (percent thresholds, charging icon, extended fields, N/A fallbacks).
class BatteryVectorWidget : public BaseWidget {
 public:
  BatteryVectorWidget(const json& options, const json& initial, EventBus& eventBus, QWidget* host)
      : BaseWidget("battery", eventBus, "battery-widget ", 0, host),
        o_(options),
        label_(qs(options.at("label"))),
        labelAlt_(qs(options.at("label_alt"))) {
    buildWidgetLabel(label_, labelAlt_);
    update(initial);
  }

  void apply(const json& step) {
    if (step.contains("toggle")) {
      toggleAlt();
      update(state_);
      return;
    }
    update(step.at("battery"));
  }

 private:
  static json defaults() {
    return {{"percent", 50},          {"is_charging", false},   {"power_plugged", false},
            {"time_remaining", 7380}, {"rate", 8.25},           {"voltage", 12.345},
            {"capacity", 40000},      {"full_capacity", 50000}, {"designed_capacity", 55000},
            {"temperature", 31.26},   {"cycle_count", 212},     {"chemistry", "LION"},
            {"health_percent", 90.9}};
  }

  static std::string fixedOrNa(const json& v, int precision, bool absolute = false) {
    if (v.is_null()) return "N/A";
    const double d = v.get<double>();
    return formatFixed(absolute ? std::fabs(d) : d, precision);
  }
  static std::string strOrNa(const json& v) {
    if (v.is_null()) return "N/A";
    return v.is_string() ? v.get<std::string>() : v.dump();
  }

  std::string threshold(int percent) const {
    const json& t = o_.at("status_thresholds");
    if (percent <= t.at("critical").get<int>()) return "critical";
    if (percent <= t.at("low").get<int>()) return "low";
    if (percent <= t.at("medium").get<int>()) return "medium";
    if (percent <= t.at("high").get<int>()) return "high";
    return "full";
  }

  std::string timeRemaining(const json& s) const {
    const long long secs = s.at("time_remaining").get<long long>();
    if (secs == -2) return o_.value("time_remaining_unlimited_icon", "unlimited");
    if (secs < 0) return "unknown";
    return o_.value("time_remaining_natural", false) ? naturalDelta(secs) : timedeltaStr(secs);
  }

  void update(const json& spec) {
    const bool alt = showAlt();
    const auto& active = alt ? labelsAlt() : labels();
    const auto parts = splitLabelParts(alt ? labelAlt_ : label_, alt);
    if (spec.is_null()) {
      state_ = nullptr;
      if (o_.value("hide_unsupported", true)) {
        hide();
        return;
      }
      for (size_t i = 0; i < parts.size() && i < active.size(); ++i) {
        if (parts[i].isIcon) active[i]->hide();
        active[i]->setText(QStringLiteral("Battery info not available"));
      }
      return;
    }
    state_ = spec;
    json s = defaults();
    s.update(spec);

    const int percent = s.at("percent").get<int>();
    const std::string original = threshold(percent);
    const bool charging = s.at("is_charging").get<bool>();
    const bool plugged = s.at("power_plugged").get<bool>();
    const std::string status = charging ? "charging" : original;
    const std::string powerClass = plugged ? "" : "discharging";

    const json& icons = o_.at("status_icons");
    std::string icon = icons.at("icon_" + original).get<std::string>();
    if (charging || plugged) {
      icon = pyFormat(o_.at("charging_options").value("icon_format", ""),
                      {{"charging_icon", icons.at("icon_charging")}, {"icon", icon}});
    }

    const std::vector<std::pair<QString, QString>> pairs = {
        {"{percent}", qs(std::to_string(percent))},
        {"{time_remaining}", qs(timeRemaining(s))},
        {"{is_charging}", charging ? "yes" : "no"},
        {"{icon}", qs(icon)},
        {"{power}", qs(fixedOrNa(s.at("rate"), 1, true))},
        {"{voltage}", qs(fixedOrNa(s.at("voltage"), 2))},
        {"{capacity}", qs(strOrNa(s.at("capacity")))},
        {"{full_capacity}", qs(strOrNa(s.at("full_capacity")))},
        {"{designed_capacity}", qs(strOrNa(s.at("designed_capacity")))},
        {"{temperature}", qs(fixedOrNa(s.at("temperature"), 1))},
        {"{cycle_count}", qs(strOrNa(s.at("cycle_count")))},
        {"{health}", qs(fixedOrNa(s.at("health_percent"), 1))},
        {"{chemistry}",
         qs(s.at("chemistry").is_null() || s.at("chemistry") == "" ? "N/A"
                                                                   : strOrNa(s.at("chemistry")))},
    };

    auto joined = [](const QString& a) { return a.simplified(); };
    size_t idx = 0;
    for (const LabelPart& part : parts) {
      if (idx >= active.size()) break;
      const QString text = replacePlaceholders(part.text, pairs);
      QString target;
      if (part.isIcon) {
        QString existing = active[idx]->property("class").toString();
        existing.remove(QRegularExpression(QStringLiteral("status-\\w+|\\bdischarging\\b")));
        active[idx]->setText(text);
        target = joined(existing + " status-" + qs(status) + " " + qs(powerClass));
      } else {
        QString out = text;
        try {
          out = qs(pyFormat(text.toStdString(), json::object()));
        } catch (const FormatError&) {
        }
        active[idx]->setText(out);
        target = joined(QStringLiteral("label ") + (alt ? "alt" : "") + " status-" + qs(status) +
                        " " + qs(powerClass));
      }
      setLabelClass(active[idx], target);
      ++idx;
    }
  }

  json o_;
  json state_;
  QString label_;
  QString labelAlt_;
};

json snapshot(BaseWidget& w, QWidget&) {
  auto dump = [&](const std::vector<QLabel*>& labels) {
    json arr = json::array();
    for (QLabel* l : labels) {
      arr.push_back({{"class", l->property("class").toString().toStdString()},
                     {"hidden", l->isHidden()},
                     {"text", l->text().toStdString()}});
    }
    return arr;
  };
  json order = json::array();
  QLayout* layout = w.labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const auto* label = qobject_cast<QLabel*>(layout->itemAt(i)->widget());
    const bool primary = std::find(w.labels().begin(), w.labels().end(), label) != w.labels().end();
    order.push_back(primary ? "primary" : "alt");
  }
  return {{"alt", dump(w.labelsAlt())},
          {"container_class", w.labelContainer()->property("class").toString().toStdString()},
          {"frame_class", w.widgetFrame()->property("class").toString().toStdString()},
          {"layout_order", order},
          {"primary", dump(w.labels())},
          {"widget_hidden", w.isHidden()}};
}

void replay(const std::string& file, bool memory, bool battery = false) {
  std::ifstream in(std::string(YASB_VECTORS_DIR) + "/labels/" + file, std::ios::binary);
  ASSERT_TRUE(in.good());
  const json cases = json::parse(in);
  ASSERT_GT(cases.size(), 0u);
  for (const json& c : cases) {
    SCOPED_TRACE(file + " / " + c.at("scenario").get<std::string>());
    QWidget host;
    EventBus eventBus;
    std::function<void(const json&)> apply;
    BaseWidget* w;
    if (battery) {
      auto* b = new BatteryVectorWidget(c.at("options"), c.at("initial_data"), eventBus, &host);
      apply = [b](const json& s) { b->apply(s); };
      w = b;
    } else {
      auto* v = new VectorWidget(memory, c.at("options"), eventBus, &host);
      apply = [v](const json& s) { v->apply(s); };
      w = v;
    }
    EXPECT_EQ(snapshot(*w, host), c.at("initial")) << snapshot(*w, host).dump(1);
    size_t i = 0;
    for (const json& s : c.at("steps")) {
      SCOPED_TRACE("step " + std::to_string(i++) + " " + s.at("step").dump());
      apply(s.at("step"));
      EXPECT_EQ(snapshot(*w, host), s.at("snapshot")) << snapshot(*w, host).dump(1);
    }
  }
}

}  // namespace

TEST(LabelFormat, PyFormatNestedLookupsAndEscapes) {
  const json info = {{"percent", {{"total", 12}, {"core", {5, 7}}}}, {"name", "cpu"}};
  EXPECT_EQ(pyFormat("{info[percent][total]}%", {{"info", info}}), "12%");
  EXPECT_EQ(pyFormat("{{literal}} {info[name]}", {{"info", info}}), "{literal} cpu");
  EXPECT_EQ(pyFormat("{info[percent]}", {{"info", info}}), "{'core': [5, 7], 'total': 12}");
  EXPECT_EQ(pyFormat("{x:>5}|{y:.1f}|{z}", {{"x", 7}, {"y", 8.25}, {"z", 50.0}}), "    7|8.2|50.0");
  EXPECT_THROW(pyFormat("{missing}", json::object()), FormatError);
  EXPECT_THROW(pyFormat("{a", {{"a", 1}}), FormatError);
}

TEST(LabelFormat, OversizedWidthOrIndexIsAFormatError) {
  EXPECT_THROW(pyFormat("{v:99999999999}", {{"v", 1}}), FormatError);
  EXPECT_THROW(pyFormat("{v:.99999999999f}", {{"v", 1.0}}), FormatError);
  EXPECT_THROW(pyFormat("{v[99999999999]}", {{"v", json::array({1})}}), FormatError);
}

namespace {

// The std::regex format-spec path the scanner replaced, kept as the oracle.
std::string oracleSpec(const json& v, const std::string& spec) {
  static const std::regex re(R"(^(?:(.)?([<>^]))?([0-9]+)?(?:\.([0-9]+))?([fds%])?$)");
  std::smatch m;
  if (!std::regex_match(spec, m, re)) throw FormatError("unsupported format spec: " + spec);
  const char fill = m[1].matched ? m[1].str()[0] : ' ';
  const std::string align = m[2].matched ? m[2].str() : "";
  const int width = m[3].matched ? std::stoi(m[3].str()) : 0;
  const bool hasPrec = m[4].matched;
  const int prec = hasPrec ? std::stoi(m[4].str()) : 0;
  const std::string type = m[5].matched ? m[5].str() : "";
  std::string body;
  const bool numeric = v.is_number();
  if (type == "f" || type == "%") {
    if (!numeric) throw FormatError("format code 'f' requires a number");
    const double d = v.get<double>();
    body = type == "%" ? formatFixed(d * 100.0, hasPrec ? prec : 6) + "%"
                       : formatFixed(d, hasPrec ? prec : 6);
  } else if (type == "d") {
    if (!v.is_number_integer()) throw FormatError("format code 'd' requires an integer");
    body = std::to_string(v.get<long long>());
  } else if (v.is_number_float() && hasPrec) {
    body = formatFixed(v.get<double>(), prec);
  } else {
    body = pyFormat("{v}", {{"v", v}});
    if (hasPrec && v.is_string() && static_cast<int>(body.size()) > prec) body.resize(prec);
  }
  if (static_cast<int>(body.size()) >= width) return body;
  const size_t pad = static_cast<size_t>(width) - body.size();
  const std::string a = align.empty() ? (numeric ? ">" : "<") : align;
  if (a == "<") return body + std::string(pad, fill);
  if (a == ">") return std::string(pad, fill) + body;
  return std::string(pad / 2, fill) + body + std::string(pad - pad / 2, fill);
}

template <class F>
std::string outcome(F&& f) {
  try {
    return "ok:" + f();
  } catch (const FormatError& e) {
    return std::string("error:") + e.what();
  }
}

}  // namespace

TEST(LabelFormat, SpecScannerMatchesRegexOnEveryShortSpec) {
  const std::string alphabet = "<>^1.fd%\n";  // the curated specs cover \r, 0, s, high bytes
  std::vector<std::string> specs{""};
  for (std::size_t begin = 0, len = 1; len <= 4; ++len) {
    const std::size_t end = specs.size();
    for (std::size_t i = begin; i < end; ++i) {
      for (const char c : alphabet) specs.push_back(specs[i] + c);
    }
    begin = end;
  }
  specs.insert(
      specs.end(),
      {"x^12.3f", "*<10", "0>8.2%", "10.10s", "<>5",    ">>",  ".",  "5.", ".f", "f5",  "1.2.3",
       "%%",      "\n>3", "\r<3",   " ^9d",   "\xC3>4", "0<5", "05", "s",  "5s", ".2s", "<0.0s"});
  const json values[] = {42, 2.5, "abc"};
  for (const auto& spec : specs) {
    for (const auto& v : values) {
      const json kwargs = {{"v", v}};
      const std::string templ = "{v:" + spec + "}";
      ASSERT_EQ(outcome([&] { return pyFormat(templ, kwargs); }),
                outcome([&] { return oracleSpec(v, spec); }))
          << "spec: " << spec << " value: " << v.dump();
    }
  }
}

TEST(LabelFormat, SpecScannerHandlesLongAndOversizedSpecs) {
  const std::string zeros(100000, '0');
  EXPECT_EQ(pyFormat("{v:" + zeros + "5}", {{"v", 1}}), "    1");
  EXPECT_EQ(pyFormat("{v:." + zeros + "2f}", {{"v", 1.0}}), "1.00");
  EXPECT_THROW(pyFormat("{v:" + std::string(100000, '<') + "}", {{"v", 1}}), FormatError);
  EXPECT_THROW(pyFormat("{v:99999999999}", {{"v", 1}}), FormatError) << "width beyond int";
  EXPECT_THROW(pyFormat("{v:.99999999999f}", {{"v", 1.0}}), FormatError);
}

TEST(LabelFormat, RoundingMatchesPython) {
  EXPECT_EQ(pyRound(36.5), 36);
  EXPECT_EQ(pyRound(37.5), 38);
  EXPECT_EQ(pyRound(24.9), 25);
  EXPECT_EQ(pyRound(0.5), 0);
  EXPECT_EQ(formatFixed(8.25, 1), "8.2");
  EXPECT_EQ(formatFixed(8.35, 1), "8.3");  // binary 8.3499... rounds down
  EXPECT_EQ(formatFixed(2.0, 2), "2.00");
}

TEST(LabelFormat, NaturalSizeIsGnuBinary) {
  EXPECT_EQ(naturalSizeGnu(0, 1), "0B");
  EXPECT_EQ(naturalSizeGnu(1023, 0), "1023B");
  EXPECT_EQ(naturalSizeGnu(1024, 1), "1.0K");
  EXPECT_EQ(naturalSizeGnu(31.6 * kGiB, 0), "32G");
  EXPECT_EQ(naturalSizeGnu(12.3 * kGiB, 1), "12.3G");
}

TEST(LabelFormat, SplitsSpansIntoIconAndLabelParts) {
  const auto parts =
      splitLabelParts(QStringLiteral("<span>\uea01</span> {a}% <span class='vpn'>V</span>"));
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_TRUE(parts[0].isIcon);
  EXPECT_EQ(parts[0].cssClass, "icon");
  EXPECT_EQ(parts[0].text, QStringLiteral("\uea01"));
  EXPECT_FALSE(parts[1].isIcon);
  EXPECT_EQ(parts[1].text, "{a}%");
  EXPECT_EQ(parts[1].cssClass, "label");
  EXPECT_EQ(parts[2].cssClass, "vpn");

  const auto alt = splitLabelParts(QStringLiteral("text"), true);
  ASSERT_EQ(alt.size(), 1u);
  EXPECT_EQ(alt[0].cssClass, "label alt");
  EXPECT_TRUE(splitLabelParts(QStringLiteral("  ")).empty());
}

TEST(LabelFormat, PlaceholdersReplaceSequentially) {
  EXPECT_EQ(replacePlaceholders("{a}-{b}-{a}", {{"{a}", "1"}, {"{b}", "{a}"}}), "1-{a}-1");
}

using LabelVectorTest = LabelFormatTest;

TEST_F(LabelVectorTest, CpuLabelsMatchPython) { replay("cpu.json", false); }

TEST_F(LabelVectorTest, MemoryLabelsMatchPython) { replay("memory.json", true); }

TEST_F(LabelVectorTest, BatteryLabelsMatchPython) { replay("battery.json", false, true); }

TEST(LabelFormat, NaturalDeltaAndTimedelta) {
  EXPECT_EQ(naturalDelta(7380), "2 hours");
  EXPECT_EQ(naturalDelta(45), "45 seconds");
  EXPECT_EQ(naturalDelta(90), "2 minutes");
  EXPECT_EQ(naturalDelta(3600), "an hour");
  EXPECT_EQ(naturalDelta(86400 * 3), "3 days");
  EXPECT_EQ(timedeltaStr(7380), "2:03:00");
  EXPECT_EQ(timedeltaStr(86400 * 3 + 5), "3 days, 0:00:05");
}

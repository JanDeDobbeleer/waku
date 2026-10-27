#include <gtest/gtest.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QTest>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

#include "core/bar/bar_window.h"

using nlohmann::json;
using namespace yasb::bar;

namespace {

const json& vectors() {
  static const json v = [] {
    std::ifstream in(std::string(YASB_VECTORS_DIR) + "/islands.json", std::ios::binary);
    return json::parse(in);
  }();
  return v;
}

std::vector<std::string> caseNames() {
  std::vector<std::string> names;
  for (const auto& [name, value] : vectors().at("cases").items()) names.push_back(name);
  return names;
}

json rectJson(const QRect& r) { return json::array({r.x(), r.y(), r.width(), r.height()}); }

constexpr int kRail = 4;

QRect vectorScreen() {
  const auto& g = vectors().at("screen_geometry");
  return QRect(g[0].get<int>(), g[1].get<int>(), g[2].get<int>(), g[3].get<int>());
}

BarSettings settingsFor(const json& config) {
  json bar = {{"style", "adaptive"},
              {"style_adaptive_exclude", {"excluded"}},
              {"animation", {{"enabled", false}}},
              {"dimensions", {{"height", vectors().at("bar_height")}}}};
  bar.merge_patch(config);
  return BarSettings::fromJson(bar);
}

QWidget* makeWidget(int width, const json& name) {
  auto* w = new QWidget;
  w->setFixedWidth(width);
  // Like BaseWidget, size through a real layout so the grid weights the columns the same way
  auto* inner = new QHBoxLayout(w);
  inner->setContentsMargins(0, 0, 0, 0);
  w->setProperty("test_widget", true);
  if (name.is_string())
    w->setProperty("widget_name", QString::fromStdString(name.get<std::string>()));
  return w;
}

struct Made {
  QPointer<BarWindow> bar;
  FakeAppBar* appBar = nullptr;
};

Made makeBar(const QString& css, const BarWindow::Widgets& groups, const json& config,
             const QString& rules = {}) {
  auto fake = std::make_unique<FakeAppBar>();
  Made made;
  made.appBar = fake.get();
  BarDeps deps;
  deps.appBar = std::move(fake);
  deps.themeDetector = [] { return false; };
  deps.screenGeometry = [] { return vectorScreen(); };
  const QString stylesheet =
      QStringLiteral(".bar { background: rgb(200, 0, 0); qproperty-railheight: %1; %2 } %3")
          .arg(kRail)
          .arg(css, rules);
  made.bar = new BarWindow(QStringLiteral("adaptive"), QStringLiteral("adaptive"),
                           QGuiApplication::primaryScreen(), stylesheet, groups,
                           settingsFor(config), std::move(deps));
  made.bar->show();
  return made;
}

void closeBar(QPointer<BarWindow>& bar) {
  if (bar) bar->close();
}

json pathJson(const QPainterPath& path) {
  json out = json::array();
  for (int i = 0; i < path.elementCount(); ++i) {
    const auto e = path.elementAt(i);
    out.push_back({static_cast<int>(e.type), e.x, e.y});
  }
  return out;
}

void expectPath(const json& expected, const QPainterPath& actual, const char* what) {
  const json got = pathJson(actual);
  ASSERT_EQ(expected.size(), got.size()) << what << " element count\n" << got.dump();
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i][0], got[i][0]) << what << "[" << i << "] type";
    EXPECT_NEAR(expected[i][1].get<double>(), got[i][1].get<double>(), 0.002)
        << what << "[" << i << "].x";
    EXPECT_NEAR(expected[i][2].get<double>(), got[i][2].get<double>(), 0.002)
        << what << "[" << i << "].y";
  }
}

json islandsJson(const IslandSpans& spans) {
  json out = json::array();
  for (const auto& [a, b] : spans) out.push_back({a, b});
  return out;
}

void expectSnapshot(const json& expected, BarWindow& bar) {
  AdaptiveBarFrame* frame = bar.adaptiveFrame();
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(expected.at("islands"), islandsJson(frame->islandSpans()));
  EXPECT_EQ(expected.at("edge_overhang").get<int>(), frame->edgeOverhang());
  EXPECT_EQ(expected.at("frame_size"), json::array({frame->width(), frame->height()}));
  EXPECT_EQ(expected.at("bar_geometry"), rectJson(bar.geometry()));
  EXPECT_EQ(expected.at("click_rect"), rectJson(frame->clickRect()));

  const auto& p = expected.at("properties");
  EXPECT_EQ(p.at("railheight").get<int>(), frame->railHeight());
  EXPECT_EQ(p.at("islandradius").get<int>(), frame->islandRadius());
  EXPECT_EQ(p.at("grouppadding").get<int>(), frame->groupPadding());
  EXPECT_EQ(p.at("islands").get<bool>(), frame->islandsEnabled());
  EXPECT_EQ(p.at("edgeradius").get<int>(), frame->edgeRadius());
  EXPECT_EQ(p.at("borderwidth").get<int>(), frame->borderWidth());
  const QColor c = frame->borderColor();
  EXPECT_EQ(p.at("bordercolor"), json::array({c.red(), c.green(), c.blue(), c.alpha()}));

  json widgets = json::array();
  QLayout* layout = frame->layout();
  for (int i = 0; i < layout->count(); ++i) {
    QWidget* container = layout->itemAt(i)->widget();
    if (!container) continue;
    for (QWidget* child : container->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
      if (!child->property("test_widget").toBool()) continue;
      const QPoint origin = child->mapTo(frame, QPoint(0, 0));
      const QVariant name = child->property("widget_name");
      widgets.push_back(
          {{"container", container->property("class").toString().toStdString()},
           {"name", name.isValid() ? json(name.toString().toStdString()) : json(nullptr)},
           {"rect", {origin.x(), origin.y(), child->width(), child->height()}}});
    }
  }
  EXPECT_EQ(expected.at("widgets"), widgets);

  expectPath(expected.at("shape"), frame->shape(), "shape");
  expectPath(expected.at("border_path"), frame->borderPath(), "border_path");
}

class IslandVectors : public testing::TestWithParam<std::string> {};

BarWindow::Widgets groupsFrom(const json& layout) {
  BarWindow::Widgets groups;
  for (const auto& entry : layout) {
    groups[QString::fromStdString(entry.at("group").get<std::string>())].push_back(
        makeWidget(entry.at("width").get<int>(), entry.at("name")));
  }
  return groups;
}

void runStandard(const json& c) {
  const json& in = c.at("inputs");
  Made made = makeBar(QString::fromStdString(in.at("bar_css").get<std::string>()),
                      groupsFrom(in.at("layout")), in.at("bar_config"),
                      QString::fromStdString(in.at("extra_rules").get<std::string>()));
  ASSERT_FALSE(made.bar.isNull());
  const json& expected = c.at("expected");
  QTRY_VERIFY_WITH_TIMEOUT(
      islandsJson(made.bar->adaptiveFrame()->islandSpans()) == expected.at("islands") &&
          rectJson(made.bar->geometry()) == expected.at("bar_geometry") &&
          json::array({made.bar->adaptiveFrame()->width(), made.bar->adaptiveFrame()->height()}) ==
              expected.at("frame_size"),
      3000);
  expectSnapshot(expected, *made.bar);
  closeBar(made.bar);
}

void runGrow(const json& c) {
  const json& in = c.at("inputs");
  auto* clock = new QWidget;
  auto* layout = new QHBoxLayout(clock);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* label = new QLabel(QStringLiteral("12:00"));
  label->setFixedWidth(in.at("label_width_before").get<int>());
  layout->addWidget(label);

  BarWindow::Widgets groups;
  groups[QStringLiteral("center")].push_back(clock);
  Made made = makeBar({}, groups, json::object());
  ASSERT_FALSE(made.bar.isNull());
  const json& ex = c.at("expected");
  QTRY_VERIFY_WITH_TIMEOUT(
      islandsJson(made.bar->adaptiveFrame()->islandSpans()) == ex.at("islands_before"), 3000);
  label->setFixedWidth(in.at("label_width_after").get<int>());
  const json expectedIslands = ex.at("islands_after");
  QTRY_VERIFY_WITH_TIMEOUT(islandsJson(made.bar->adaptiveFrame()->islandSpans()) == expectedIslands,
                           3000);
  EXPECT_EQ(ex.at("islands_after"), islandsJson(made.bar->adaptiveFrame()->islandSpans()));
  closeBar(made.bar);
}

void runEdgeChange(const json& c) {
  const json& in = c.at("inputs");
  const json& ex = c.at("expected");
  BarWindow::Widgets groups;
  groups[QStringLiteral("center")].push_back(makeWidget(120, json(nullptr)));
  Made made = makeBar(QStringLiteral("qproperty-edgeradius: 0;"), groups,
                      {{"alignment", {{"position", in.at("position")}}}});
  ASSERT_FALSE(made.bar.isNull());
  QTRY_VERIFY_WITH_TIMEOUT(rectJson(made.bar->geometry()) == ex.at("geometry_before"), 3000);
  EXPECT_EQ(ex.at("geometry_before"), rectJson(made.bar->geometry()));
  made.bar->setStyleSheet(QStringLiteral(".bar { background: rgb(200, 0, 0); qproperty-railheight: "
                                         "%1; qproperty-edgeradius: %2; }")
                              .arg(kRail)
                              .arg(in.at("edge_radius_after").get<int>()));
  EXPECT_EQ(ex.at("geometry_immediately_after_stylesheet"), rectJson(made.bar->geometry()));
  QTRY_VERIFY_WITH_TIMEOUT(rectJson(made.bar->geometry()) == ex.at("geometry_after"), 3000);
  EXPECT_EQ(ex.at("geometry_after"), rectJson(made.bar->geometry()));
  EXPECT_EQ(ex.at("bar_height_after").get<int>(), made.bar->height());
  closeBar(made.bar);
}

}  // namespace

TEST_P(IslandVectors, MatchesPython) {
  const std::string name = GetParam();
  const json& c = vectors().at("cases").at(name);
  if (name == "island_grows_with_widget") {
    runGrow(c);
  } else if (name.rfind("edgeradius_change_keeps_edge_", 0) == 0) {
    runEdgeChange(c);
  } else {
    runStandard(c);
  }
}

INSTANTIATE_TEST_SUITE_P(Islands, IslandVectors, testing::ValuesIn(caseNames()),
                         [](const auto& info) {
                           std::string n = info.param;
                           for (char& ch : n) {
                             if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '_';
                           }
                           return n;
                         });

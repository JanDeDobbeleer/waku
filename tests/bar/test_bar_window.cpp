#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QTest>
#include <nlohmann/json.hpp>

#include "core/bar/bar_window.h"
#include "core/bar/screens.h"
#include "core/css/preprocessor.h"
#include "core/css/transition_engine.h"

using namespace yasb::bar;

namespace {

class BarWindowTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    app_ = std::make_unique<QApplication>(argc_, argv_);
  }
  static void TearDownTestSuite() { app_.reset(); }

  BarDeps deps(std::function<bool()> dark = [] { return false; }) {
    BarDeps d;
    d.appBar = std::make_unique<FakeAppBar>();
    d.themeDetector = std::move(dark);
    d.screenGeometry = [] { return QRect(0, 0, 800, 800); };
    d.globalState = &globalState_;
    return d;
  }

  static BarSettings adaptive() {
    BarSettings s;
    s.style = "adaptive";
    s.className = "yasb-bar";
    s.height = 30;
    return s;
  }

  static inline int argc_ = 1;
  static inline char arg0_[11] = "yasb_tests";
  static inline char* argv_[2] = {arg0_, nullptr};
  static inline std::unique_ptr<QApplication> app_;
  GlobalState globalState_;
};

}  // namespace

TEST_F(BarWindowTest, ContainerAndFrameClassNames) {
  auto* bar = new BarWindow(QStringLiteral("id"), QStringLiteral("main"),
                            QGuiApplication::primaryScreen(), {}, {}, adaptive(), deps());
  EXPECT_EQ(bar->barFrame()->property("class").toString(), QStringLiteral("bar yasb-bar adaptive"));
  QStringList containers;
  for (QWidget* c : bar->barFrame()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
    if (c->property("class").isValid()) containers << c->property("class").toString();
  }
  EXPECT_EQ(containers, (QStringList{QStringLiteral("container container-left"),
                                     QStringLiteral("container container-center"),
                                     QStringLiteral("container container-right")}));
  bar->close();

  BarSettings plain = adaptive();
  plain.style = "bar";
  auto* flat = new BarWindow(QStringLiteral("id"), QStringLiteral("main"),
                             QGuiApplication::primaryScreen(), {}, {}, plain, deps());
  EXPECT_EQ(flat->barFrame()->property("class").toString(), QStringLiteral("bar yasb-bar"));
  EXPECT_EQ(flat->adaptiveFrame(), nullptr);
  flat->close();
}

TEST_F(BarWindowTest, UserStylesheetQPropertiesThroughPreprocessor) {
  const QString path = QDir(QStringLiteral(YASB_VECTORS_DIR))
                           .absoluteFilePath(QStringLiteral("inputs/css/user/styles.css"));
  yasb::css::CssPreprocessor pre(QDir::toNativeSeparators(path));
  yasb::css::TransitionEngine engine;
  const QString qss = engine.loadStyleSheet(pre.process());

  auto* bar = new BarWindow(QStringLiteral("id"), QStringLiteral("main"),
                            QGuiApplication::primaryScreen(), qss, {}, adaptive(), deps());
  bar->show();
  QTest::qWait(20);
  const AdaptiveBarFrame* f = bar->adaptiveFrame();
  EXPECT_EQ(f->railHeight(), 0);
  EXPECT_EQ(f->islandRadius(), 16);
  EXPECT_EQ(f->groupPadding(), 12);
  EXPECT_TRUE(f->islandsEnabled());
  EXPECT_EQ(f->edgeRadius(), 0);
  bar->close();
}

TEST_F(BarWindowTest, DarkClassTogglesAndRepolishes) {
  bool dark = false;
  const QString css = QStringLiteral(
      ".bar { qproperty-islandradius: 16; } .bar.dark { qproperty-islandradius: 7; }");
  auto* bar =
      new BarWindow(QStringLiteral("id"), QStringLiteral("main"), QGuiApplication::primaryScreen(),
                    css, {}, adaptive(), deps([&dark] { return dark; }));
  bar->show();
  QTest::qWait(20);
  EXPECT_EQ(bar->adaptiveFrame()->islandRadius(), 16);
  EXPECT_FALSE(globalState_.isDark());

  dark = true;
  bar->themeManager()->updateThemeClass();
  EXPECT_EQ(bar->barFrame()->property("class").toString(),
            QStringLiteral("bar yasb-bar adaptive dark"));
  EXPECT_EQ(bar->adaptiveFrame()->islandRadius(), 7);
  EXPECT_TRUE(globalState_.isDark());

  dark = false;
  bar->themeManager()->updateThemeClass();
  EXPECT_EQ(bar->barFrame()->property("class").toString(), QStringLiteral("bar yasb-bar adaptive"));
  EXPECT_FALSE(globalState_.isDark());
  bar->close();
}

TEST_F(BarWindowTest, AppBarRequestIsInjectedAndOnlyReservesWhenAsked) {
  auto fake = std::make_unique<FakeAppBar>();
  FakeAppBar* raw = fake.get();
  BarDeps d = deps();
  d.appBar = std::move(fake);
  BarSettings s = adaptive();
  s.windowsAppBar = true;
  s.paddingTop = 6;
  s.position = "bottom";
  auto* bar = new BarWindow(QStringLiteral("id"), QStringLiteral("main"),
                            QGuiApplication::primaryScreen(), {}, {}, s, std::move(d));
  ASSERT_EQ(raw->requests.size(), 1u);
  EXPECT_EQ(raw->requests[0].height, 36);
  EXPECT_EQ(raw->requests[0].edge, AppBarEdge::Bottom);
  EXPECT_TRUE(raw->requests[0].reserveSpace);
  bar->close();
  EXPECT_EQ(raw->removeCount, 1);
}

TEST(AppBarRect, ScalesHeightButOnlyScalesScreenWhenAsked) {
  const QRect screen(0, 0, 2560, 1707);
  const auto top = computeAppBarRect(AppBarEdge::Top, 36, screen, 1.5, true);
  EXPECT_EQ(top, (AppBarRect{0, 0, 2560, 54}));
  const auto bottom = computeAppBarRect(AppBarEdge::Bottom, 36, screen, 1.5, true);
  EXPECT_EQ(bottom, (AppBarRect{0, 2560 - 54, 2560, 2560}));
  const auto unscaled = computeAppBarRect(AppBarEdge::Bottom, 36, screen, 1.5, false);
  EXPECT_EQ(unscaled.bottom, 1707);
}

TEST(BarScreens, StarSkipsExplicitlyAssignedAndWarnsOnUnknown) {
  std::vector<std::string> warn;
  const auto out = resolveBarScreens({{"a", {"*"}}, {"b", {"primary", "missing"}}, {"c", {"**"}}},
                                     {"DISPLAY1", "DISPLAY2"}, "DISPLAY1", &warn);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[0].barName, "a");
  EXPECT_EQ(out[0].screenName, "DISPLAY2");
  EXPECT_EQ(out[1].barName, "b");
  EXPECT_EQ(out[1].screenName, "DISPLAY1");
  EXPECT_EQ(out[2].barName, "c");
  EXPECT_EQ(out[3].screenName, "DISPLAY2");
  EXPECT_FALSE(warn.empty());
}

TEST(BarScreens, ScreenByNameIsNullWhenNothingMatches) {
  EXPECT_EQ(screenByName({}, QStringLiteral("DISPLAY1")), nullptr);
}

TEST(BarSettingsParse, ReadsNormalizedBarObject) {
  const auto bar = nlohmann::json::parse(R"({"alignment":{"align":"right","position":"bottom"},
    "class_name":"x","dimensions":{"height":36,"width":"90%"},"padding":{"left":1,"right":2,"top":3,"bottom":4},
    "style":"adaptive","style_adaptive_exclude":["a"],"window_flags":{"windows_app_bar":true,"always_on_top":false},
    "layouts":{"center":{"alignment":"left","stretch":false}}})");
  const BarSettings s = BarSettings::fromJson(bar);
  EXPECT_EQ(s.position, "bottom");
  EXPECT_EQ(s.align, "right");
  EXPECT_EQ(s.width, "90%");
  EXPECT_EQ(s.paddingBottom, 4);
  EXPECT_TRUE(s.windowsAppBar);
  EXPECT_FALSE(s.alwaysOnTop);
  EXPECT_FALSE(s.center.stretch);
  EXPECT_EQ(s.styleAdaptiveExclude, std::vector<std::string>{"a"});
}

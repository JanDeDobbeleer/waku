#include <gtest/gtest.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QRegularExpression>
#include <QTest>
#include <QWidget>
#include <memory>

#include "core/css/transition_engine.h"

using namespace yasb::css;

namespace {

QStringList* g_warnings = nullptr;
QtMessageHandler g_prev = nullptr;

void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  if (type == QtWarningMsg && g_warnings && QByteArray(ctx.category) == "yasb.css.transitions") {
    g_warnings->push_back(msg);
    return;
  }
  if (g_prev) g_prev(type, ctx, msg);
}

class Transitions : public testing::Test {
 protected:
  void SetUp() override {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    app = std::make_unique<QApplication>(argc, argv);
    g_warnings = &warnings;
    g_prev = qInstallMessageHandler(capture);
  }
  void TearDown() override {
    qInstallMessageHandler(g_prev);
    g_warnings = nullptr;
  }

  static int redOf(const QString& rgba) {
    static const QRegularExpression re(QStringLiteral("rgba\\((\\d+),"));
    const auto m = re.match(rgba);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
  }

  int argc = 1;
  char arg0[11] = "yasb_tests";
  char* argv[2] = {arg0, nullptr};
  std::unique_ptr<QApplication> app;
  QStringList warnings;
};

QWidget* makeWidget(const char* cls) {
  auto* w = new QWidget;
  w->setProperty("class", QString::fromLatin1(cls));
  w->resize(100, 40);
  return w;
}

}  // namespace

TEST_F(Transitions, BackgroundColorInterpolatesOnHover) {
  TransitionEngine engine;
  const QString qss = engine.loadStyleSheet(QStringLiteral(
      ".widget { background-color: #202020; transition: background-color 0.3s linear; }\n"
      ".widget:hover { background-color: #404040; }"));
  app->setStyleSheet(qss);
  engine.install(app.get());

  std::unique_ptr<QWidget> w(makeWidget("widget"));
  w->show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(w.get()));
  w->ensurePolished();

  QEvent enter(QEvent::HoverEnter);
  QElapsedTimer clock;
  clock.start();
  QCoreApplication::sendEvent(w.get(), &enter);

  QTest::qWait(150);
  const int mid = redOf(engine.currentValue(w.get(), QStringLiteral("background-color")));
  EXPECT_GT(mid, 0x20) << "should have started moving";
  EXPECT_LT(mid, 0x40) << "should not have finished at ~half time";

  QTest::qWait(300);
  EXPECT_GE(clock.elapsed(), 300);
  EXPECT_EQ(redOf(engine.currentValue(w.get(), QStringLiteral("background-color"))), 0x40);

  QEvent leave(QEvent::HoverLeave);
  QCoreApplication::sendEvent(w.get(), &leave);
  QTest::qWait(450);
  EXPECT_EQ(redOf(engine.currentValue(w.get(), QStringLiteral("background-color"))), 0x20);
  EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(Transitions, ClickedOpacityDipsThenReturns) {
  TransitionEngine engine;
  app->setStyleSheet(engine.loadStyleSheet(
      QStringLiteral(".btn { opacity: 1; transition: opacity 100ms linear; }\n"
                     ".btn:clicked { opacity: 0.5; }")));
  engine.install(app.get());

  std::unique_ptr<QWidget> w(makeWidget("btn"));
  w->show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(w.get()));
  w->ensurePolished();

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(w.get(), &press);

  double minOpacity = 1.0;
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < 600) {
    QTest::qWait(10);
    if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect())) {
      minOpacity = std::min(minOpacity, e->opacity());
    }
  }
  EXPECT_LE(minOpacity, 0.56);
  EXPECT_GE(minOpacity, 0.49);
  EXPECT_EQ(w->graphicsEffect(), nullptr) << "opacity returned to 1 and the effect was removed";
}

TEST_F(Transitions, BoxShadowWarnsOnce) {
  TransitionEngine engine;
  engine.loadStyleSheet(QStringLiteral(".card { box-shadow: 0 2px 4px #000; color: red; }"));
  ASSERT_EQ(warnings.size(), 1);
  EXPECT_TRUE(warnings[0].contains(QStringLiteral(".card")));
  EXPECT_TRUE(warnings[0].contains(QStringLiteral("box-shadow")));
  EXPECT_TRUE(warnings[0].contains(QStringLiteral("line 1")));
}

namespace {

// A hidden widget hovered with sendEvent: no animation runs, the target is written at once.
int hoverRed(TransitionEngine& engine, QWidget* w) {
  QEvent leave(QEvent::HoverLeave);
  QCoreApplication::sendEvent(w, &leave);
  QEvent enter(QEvent::HoverEnter);
  QCoreApplication::sendEvent(w, &enter);
  static const QRegularExpression re(QStringLiteral("rgba\\((\\d+),"));
  const auto m = re.match(engine.currentValue(w, QStringLiteral("background-color")));
  return m.hasMatch() ? m.captured(1).toInt() : -1;
}

}  // namespace

TEST_F(Transitions, CachedCascadeFollowsOwnPropertyChange) {
  TransitionEngine engine;
  app->setStyleSheet(engine.loadStyleSheet(
      QStringLiteral(".a { background-color: #200000; }\n"
                     ".a:hover { background-color: #400000; }\n"
                     ".a[active=\"true\"]:hover { background-color: #800000; }")));
  engine.install(app.get());
  std::unique_ptr<QWidget> w(makeWidget("a"));
  EXPECT_EQ(hoverRed(engine, w.get()), 0x40);
  w->setProperty("active", QStringLiteral("true"));  // re-evaluates while hovered
  EXPECT_EQ(redOf(engine.currentValue(w.get(), QStringLiteral("background-color"))), 0x80);
  w->setProperty("active", QStringLiteral("false"));
  EXPECT_EQ(redOf(engine.currentValue(w.get(), QStringLiteral("background-color"))), 0x40);
  w->setProperty("class", QStringLiteral("b"));
  EXPECT_EQ(hoverRed(engine, w.get()), -1) << "no rule matches any more";
}

TEST_F(Transitions, CachedCascadeFollowsAncestorAndNameChanges) {
  TransitionEngine engine;
  app->setStyleSheet(
      engine.loadStyleSheet(QStringLiteral(".a { background-color: #100000; }\n"
                                           ".a:hover { background-color: #200000; }\n"
                                           ".p .a:hover { background-color: #400000; }\n"
                                           ".q .a:hover { background-color: #600000; }\n"
                                           "#named:hover { background-color: #800000; }")));
  engine.install(app.get());
  QWidget p;
  p.setProperty("class", QStringLiteral("p"));
  QWidget q;
  q.setProperty("class", QStringLiteral("x"));
  QWidget* w = makeWidget("a");
  w->setParent(&p);
  EXPECT_EQ(hoverRed(engine, w), 0x40);
  w->setParent(&q);
  EXPECT_EQ(hoverRed(engine, w), 0x20) << "reparented under a non-matching ancestor";
  q.setProperty("class", QStringLiteral("q"));
  EXPECT_EQ(hoverRed(engine, w), 0x60) << "ancestor class change";
  w->setObjectName(QStringLiteral("named"));
  EXPECT_EQ(hoverRed(engine, w), 0x80) << "object name change";
  q.setObjectName(QStringLiteral("renamed"));
  w->setObjectName(QString());
  EXPECT_EQ(hoverRed(engine, w), 0x60);
}

TEST_F(Transitions, CachedCascadeIsDroppedOnStyleSheetReload) {
  TransitionEngine engine;
  app->setStyleSheet(engine.loadStyleSheet(QStringLiteral(
      ".a { background-color: #100000; }\n.a:hover { background-color: #200000; }")));
  engine.install(app.get());
  std::unique_ptr<QWidget> w(makeWidget("a"));
  EXPECT_EQ(hoverRed(engine, w.get()), 0x20);
  app->setStyleSheet(engine.loadStyleSheet(QStringLiteral(
      ".a { background-color: #100000; }\n.a:hover { background-color: #300000; }")));
  EXPECT_EQ(hoverRed(engine, w.get()), 0x30);
}

// Micro-benchmark: 200 widgets in 20 groups, 120 rules, a mix of hover/focus/release events,
// property changes and events the engine ignores. Prints the time; the budget only catches
// gross regressions (about 5x the arm64-debug time after the per-widget cascade cache).
TEST_F(Transitions, EventFilterThroughputBenchmark) {
  constexpr int kGroups = 20;
  constexpr int kPerGroup = 10;
  constexpr int kKinds = 30;
  constexpr int kEvents = 4000;

  QString css;
  for (int k = 0; k < kKinds; ++k) {
    css += QStringLiteral(
               ".k%1 { color: #336699; padding: 2px; transition: background-color 200ms; }\n")
               .arg(k);
    // Most hover rules restate the base value; one kind in ten really changes on hover.
    css += QStringLiteral(".group .k%1:hover { %2 }\n")
               .arg(k)
               .arg(k % 10 == 0 ? QStringLiteral("background-color: #804020;")
                                : QStringLiteral("padding: 2px;"));
    css += QStringLiteral(".k%1[active=\"true\"] { margin: 1px; }\n").arg(k);
    css += QStringLiteral("#item%1:focus { color: #336699; }\n").arg(k);
  }
  TransitionEngine engine;
  app->setStyleSheet(engine.loadStyleSheet(css));
  engine.install(app.get());

  QWidget root;
  std::vector<QWidget*> widgets;
  for (int g = 0; g < kGroups; ++g) {
    auto* group = new QWidget(&root);
    group->setProperty("class", QStringLiteral("group g%1").arg(g));
    for (int i = 0; i < kPerGroup; ++i) {
      const int n = g * kPerGroup + i;
      auto* w = new QWidget(group);
      w->setObjectName(QStringLiteral("item%1").arg(n));
      w->setProperty("class", QStringLiteral("widget k%1 label").arg(n % kKinds));
      w->setProperty("active", QStringLiteral("false"));
      widgets.push_back(w);
    }
  }
  QObject plain;
  const QEvent::Type cycle[] = {
      QEvent::HoverEnter,    QEvent::FocusIn,           QEvent::User,  QEvent::HoverLeave,
      QEvent::FocusOut,      QEvent::MouseMove,         QEvent::Enter, QEvent::Leave,
      QEvent::UpdateRequest, QEvent::MouseButtonRelease};

  QElapsedTimer clock;
  clock.start();
  for (int e = 0; e < kEvents; ++e) {
    // Each widget gets the whole cycle in a row, so hover and focus really toggle.
    QWidget* w = widgets[(e / std::size(cycle) * 7) % widgets.size()];
    if (e % 500 == 0) {
      w->setProperty("active", e % 1000 == 0 ? QStringLiteral("true") : QStringLiteral("false"));
      continue;
    }
    QEvent ev(cycle[e % std::size(cycle)]);
    engine.eventFilter(w, &ev);
    engine.eventFilter(&plain, &ev);
  }
  const qint64 ms = clock.elapsed();
  std::printf("[ bench    ] transition engine: %d events over %zu widgets in %lld ms\n", kEvents,
              widgets.size(), static_cast<long long>(ms));
  RecordProperty("elapsed_ms", static_cast<int>(ms));
  // arm64-debug: about 6 s before the cascade cache, about 0.55 s after.
  EXPECT_LT(ms, 3000);
}

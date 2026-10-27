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
  app->setStyleSheet(engine.loadStyleSheet(QStringLiteral(
      ".btn { opacity: 1; transition: opacity 100ms linear; }\n"
      ".btn:clicked { opacity: 0.5; }")));
  engine.install(app.get());

  std::unique_ptr<QWidget> w(makeWidget("btn"));
  w->show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(w.get()));
  w->ensurePolished();

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
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

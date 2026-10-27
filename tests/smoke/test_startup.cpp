#include <gtest/gtest.h>

#include <QApplication>

TEST(Startup, QApplicationCreatesOffscreen) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  int argc = 1;
  char arg0[] = "yasb_tests";
  char* argv[] = {arg0, nullptr};
  QApplication app(argc, argv);
  EXPECT_EQ(QGuiApplication::platformName(), QStringLiteral("offscreen"));
}

#include <gtest/gtest.h>

#include <QApplication>

TEST(Startup, SharedQApplicationUsesOffscreenPlatform) {
  ASSERT_NE(QApplication::instance(), nullptr);
  EXPECT_EQ(QGuiApplication::platformName(), QStringLiteral("offscreen"));
}

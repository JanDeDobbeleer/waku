#include <gtest/gtest.h>

#include <QString>

#include "kernel/win32/shell_icons.h"

using wabar::kernel::IconBitmap;

TEST(KernelShellIcons, NullBitmapReportsNull) {
  IconBitmap bitmap;
  EXPECT_TRUE(bitmap.isNull());
  bitmap.width = 2;
  bitmap.height = 2;
  EXPECT_TRUE(bitmap.isNull());
  bitmap.bgra = QByteArray(16, 'x');
  EXPECT_FALSE(bitmap.isNull());
}

TEST(KernelShellIcons, MissingItemGivesNullBitmap) {
  EXPECT_TRUE(
      wabar::kernel::shellItemIcon(QStringLiteral("C:\\wabar-no-such-folder\\x.exe"), 32).isNull());
  EXPECT_TRUE(wabar::kernel::shellItemIcon(QString(), 32).isNull());
  EXPECT_TRUE(wabar::kernel::iconBitmapFromHicon(nullptr).isNull());
}

// Reads the system's own explorer.exe icon: read only, no desktop state involved.
TEST(KernelShellIcons, SystemFileHasAnIcon) {
  const IconBitmap bitmap =
      wabar::kernel::shellItemIcon(QStringLiteral("C:\\Windows\\explorer.exe"), 32);
  ASSERT_FALSE(bitmap.isNull());
  EXPECT_GT(bitmap.width, 0);
  EXPECT_EQ(bitmap.bgra.size(), qsizetype(bitmap.width) * bitmap.height * 4);
}

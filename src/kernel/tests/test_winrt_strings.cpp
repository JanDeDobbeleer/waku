#include <gtest/gtest.h>

#include "kernel/win32/winrt_strings.h"

using waku::kernel::toHString;
using waku::kernel::toQString;

TEST(WinrtStrings, RoundTripsTextOutsideLatin1) {
  const QString text = QStringLiteral("Café ♫ \U0001F3B5");
  const winrt::hstring wide = toHString(text);
  EXPECT_EQ(wide.size(), static_cast<std::uint32_t>(text.size()));
  EXPECT_EQ(toQString(wide), text);
}

TEST(WinrtStrings, KeepsEmbeddedNuls) {
  const wchar_t raw[] = {L'a', L'\0', L'b'};
  const winrt::hstring wide(raw, 3);
  const QString text = toQString(wide);
  EXPECT_EQ(text.size(), 3);
  EXPECT_EQ(text.at(2), QLatin1Char('b'));
  EXPECT_EQ(toHString(text), wide);
}

TEST(WinrtStrings, EmptyStaysEmpty) {
  EXPECT_TRUE(toQString(winrt::hstring()).isEmpty());
  EXPECT_TRUE(toHString(QString()).empty());
}

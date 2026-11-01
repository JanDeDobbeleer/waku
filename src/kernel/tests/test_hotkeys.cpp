// kernel::Hotkeys and parseHotkey on the fake backend: no test registers a real hotkey on the
// machine.
#include <gtest/gtest.h>

#include "kernel/qt/hotkeys.h"
#include "kernel/testing/hotkeys_fixtures.h"

using namespace wabar::kernel;

namespace {

HotkeyCombo combo(const char* keys) { return parseHotkey(QString::fromLatin1(keys)).value(); }

}  // namespace

TEST(HotkeysParse, ReadsModifiersAndKeys) {
  const auto altSpace = parseHotkey(QStringLiteral("Alt + Space"));
  ASSERT_TRUE(altSpace.has_value());
  EXPECT_EQ(altSpace->modifiers, 1u);
  EXPECT_EQ(altSpace->virtualKey, 0x20u);
  const auto altE = parseHotkey(QStringLiteral(" Alt + E "));
  ASSERT_TRUE(altE.has_value());
  EXPECT_EQ(altE->modifiers, 1u);
  EXPECT_EQ(altE->virtualKey, static_cast<unsigned>('E'));
  const auto f5 = parseHotkey(QStringLiteral("ctrl+shift+f5"));
  ASSERT_TRUE(f5.has_value());
  EXPECT_EQ(f5->modifiers, 6u);
  EXPECT_EQ(f5->virtualKey, 0x74u);
  EXPECT_EQ(parseHotkey(QStringLiteral("win+space"))->modifiers, 8u);
  EXPECT_FALSE(parseHotkey(QStringLiteral("alt")).has_value());
  EXPECT_FALSE(parseHotkey(QStringLiteral("a+b")).has_value());
  EXPECT_FALSE(parseHotkey(QStringLiteral("f25")).has_value());
  EXPECT_FALSE(parseHotkey(QStringLiteral("f99")).has_value());
  EXPECT_FALSE(parseHotkey(QString()).has_value());
}

TEST(HotkeysRegistry, SharesOneRegistrationAndReleasesWithTheLastHandle) {
  ScopedFakeHotkeys fake;
  Hotkeys& hotkeys = Hotkeys::instance();
  int a = 0;
  int b = 0;
  const auto h1 = hotkeys.add(combo("alt+e"), [&] { ++a; });
  const auto h2 = hotkeys.add(combo("alt+e"), [&] { ++b; });
  ASSERT_GT(h1, 0);
  ASSERT_GT(h2, 0);
  EXPECT_EQ(fake.backend->registered.size(), 1u);
  EXPECT_TRUE(fake.backend->press(combo("alt+e")));
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 1);
  hotkeys.remove(h1);
  EXPECT_EQ(fake.backend->registered.size(), 1u);
  hotkeys.remove(h2);
  EXPECT_TRUE(fake.backend->registered.empty());
  hotkeys.remove(h2);  // unknown handle: ignored
}

TEST(HotkeysRegistry, ATakenCombinationFailsAndAnInertDesktopHasNoBackend) {
  {
    ScopedFakeHotkeys fake;
    fake.backend->taken.insert(combo("alt+e"));
    EXPECT_EQ(Hotkeys::instance().add(combo("alt+e"), [] {}), 0);
    EXPECT_GT(Hotkeys::instance().add(combo("alt+f"), [] {}), 0);
  }
  // The offscreen platform is not a live desktop: the default backend is never built.
  Hotkeys::instance().resetForTests();
  EXPECT_FALSE(Hotkeys::instance().active());
  EXPECT_EQ(Hotkeys::instance().add(combo("alt+e"), [] {}), 0);
  Hotkeys::instance().resetForTests();
}

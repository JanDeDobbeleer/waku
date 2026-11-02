// The wallpaper gallery hotkey and GlobalShortcut use one kernel::Hotkeys table: a combination
// both ask for is one registration, and a key press fires both. On the fake backend only: no test
// registers a real hotkey on the machine.
#include <gtest/gtest.h>

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <memory>

#include "features/wallpapers/qml/wallpapers_service.h"
#include "features/wallpapers/testing/fake_wallpaper_backend.h"
#include "host/qml/global_shortcut.h"
#include "kernel/qt/hotkeys.h"
#include "kernel/testing/hotkeys_fixtures.h"

using waku::host::GlobalShortcut;
using waku::kernel::HotkeyCombo;
using waku::kernel::parseHotkey;
using waku::kernel::ScopedFakeHotkeys;
using waku::wallpapers::DesktopState;
using waku::wallpapers::FakeBackend;
using waku::wallpapers::Wallpapers;

namespace {

HotkeyCombo combo(const char* keys) { return parseHotkey(QString::fromLatin1(keys)).value(); }

// A wallpaper service on a fake desktop over a folder with one image.
std::unique_ptr<Wallpapers> makeService(const QTemporaryDir& folder) {
  QImage image(QSize(8, 8), QImage::Format_RGB32);
  image.fill(Qt::red);
  EXPECT_TRUE(image.save(QDir(folder.path()).filePath(QStringLiteral("a.png"))));
  auto state = std::make_shared<DesktopState>();
  Wallpapers::Deps deps;
  deps.backend = [state] { return std::make_unique<FakeBackend>(state); };
  deps.picker = [](int) { return 0; };
  deps.realBackend = false;
  return std::make_unique<Wallpapers>(std::move(deps), nullptr);
}

}  // namespace

TEST(HotkeysShared, TheGalleryAndAGlobalShortcutShareOneRegistration) {
  ScopedFakeHotkeys fake;  // first, so the users go before it
  QTemporaryDir folder;
  ASSERT_TRUE(folder.isValid());
  auto service = makeService(folder);
  ASSERT_TRUE(service->available());
  service->configure(QDir::fromNativeSeparators(folder.path()), QStringLiteral("alt+e"),
                     QStringLiteral("primary"), 0);
  ASSERT_EQ(fake.backend->registered.size(), 1u);

  auto shortcut = std::make_unique<GlobalShortcut>();
  shortcut->setName(QStringLiteral("same-keys"));
  shortcut->setKeys(QStringLiteral("alt+e"));
  shortcut->componentComplete();
  EXPECT_TRUE(shortcut->isRegistered());
  // One table: the second user joins the first registration instead of a second RegisterHotKey.
  EXPECT_EQ(fake.backend->registered.size(), 1u);
  EXPECT_EQ(fake.backend->registerCalls, 1);

  int presses = 0;
  QObject::connect(shortcut.get(), &GlobalShortcut::pressed, [&] { ++presses; });
  ASSERT_TRUE(fake.backend->press(combo("alt+e")));
  EXPECT_EQ(presses, 1);
  EXPECT_TRUE(service->galleryOpen());

  // The combination stays registered while either user holds it.
  service.reset();
  EXPECT_EQ(fake.backend->registered.size(), 1u);
  ASSERT_TRUE(fake.backend->press(combo("alt+e")));
  EXPECT_EQ(presses, 2);
  shortcut.reset();
  EXPECT_TRUE(fake.backend->registered.empty());
}

TEST(HotkeysShared, ACombinationAnotherProgramOwnsFailsForBoth) {
  ScopedFakeHotkeys fake;
  fake.backend->taken.insert(combo("alt+e"));
  QTemporaryDir folder;
  ASSERT_TRUE(folder.isValid());
  auto service = makeService(folder);
  service->configure(QDir::fromNativeSeparators(folder.path()), QStringLiteral("alt+e"), QString(),
                     0);
  GlobalShortcut shortcut;
  shortcut.setKeys(QStringLiteral("alt+e"));
  shortcut.componentComplete();
  EXPECT_FALSE(shortcut.isRegistered());
  EXPECT_TRUE(fake.backend->registered.empty());
  EXPECT_EQ(service->files().size(), 1);  // the service still works
}

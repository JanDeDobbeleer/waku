// The Wallpapers service on a fake IDesktopWallpaper and a temporary folder of tiny images. The
// real wallpaper setter and the real hotkey never run: the service gets a fake backend and a
// fake hotkey host.
#include <gtest/gtest.h>

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>
#include <vector>

#include "features/wallpapers/qml/wallpapers_service.h"
#include "features/wallpapers/testing/fake_wallpaper_backend.h"
#include "host/testing/shell_fixtures.h"

namespace {

namespace plat = wabar::wallpapers;
using wabar::wallpapers::DesktopState;
using wabar::wallpapers::FakeBackend;
using wabar::wallpapers::FakeHotkeys;
using wabar::wallpapers::HotkeyCombo;
using wabar::wallpapers::HotkeyHost;
using wabar::wallpapers::parseHotkey;
using wabar::wallpapers::Wallpapers;

// The wallpaper "on the desktop": shared by every backend the service creates.
void makeImage(const QString& path, const QColor& color, QSize size = QSize(40, 20)) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(color);
  ASSERT_TRUE(image.save(path));
}

struct Fixture {
  Fixture() {
    EXPECT_TRUE(folder.isValid());
    EXPECT_TRUE(cacheFolder.isValid());
    makeImage(folder.filePath(QStringLiteral("img10.png")), Qt::red);
    makeImage(folder.filePath(QStringLiteral("img2.png")), Qt::green);
    makeImage(folder.filePath(QStringLiteral("a.jpg")), Qt::blue);
    QFile notes(folder.filePath(QStringLiteral("notes.txt")));
    EXPECT_TRUE(notes.open(QIODevice::WriteOnly));

    state = std::make_shared<DesktopState>();
    auto owned = std::make_unique<FakeHotkeys>();
    hotkeys = owned.get();
    Wallpapers::Deps deps;
    deps.backend = [state = state] { return std::make_unique<FakeBackend>(state); };
    deps.picker = [this](int count) { return picks[next++ % picks.size()] % count; };
    deps.hotkeys = std::move(owned);
    deps.realBackend = false;
    service = std::make_unique<Wallpapers>(std::move(deps), nullptr);
  }

  QString file(const QString& name) const {
    return QDir::toNativeSeparators(QDir(folder.path()).absoluteFilePath(name));
  }
  QStringList names() const {
    QStringList result;
    for (const QVariant& row : service->files())
      result << row.toMap().value(QStringLiteral("name")).toString();
    return result;
  }

  QTemporaryDir folder;
  QTemporaryDir cacheFolder;
  std::shared_ptr<DesktopState> state;
  FakeHotkeys* hotkeys = nullptr;
  std::unique_ptr<Wallpapers> service;
  std::vector<int> picks{0};
  size_t next = 0;
};

}  // namespace

TEST(WallpapersService, ListsImagesInExplorerOrder) {
  Fixture f;
  EXPECT_TRUE(f.service->available());
  QSignalSpy spy(f.service.get(), &Wallpapers::filesChanged);
  f.service->setImagePath(f.folder.path());
  EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(f.names(), (QStringList{QStringLiteral("a.jpg"), QStringLiteral("img2.png"),
                                    QStringLiteral("img10.png")}));
  const QVariantMap first = f.service->files().first().toMap();
  EXPECT_FALSE(first.contains(QStringLiteral("thumbnail")));
  EXPECT_FALSE(first.value(QStringLiteral("current")).toBool());
  // A rescan that finds the same files keeps the list: a gallery keeps its delegates.
  f.service->refresh();
  EXPECT_EQ(spy.count(), 1);
}

TEST(WallpapersService, SettingAWallpaperUpdatesCurrentAndTheFlagOnItsRow) {
  Fixture f;
  f.service->setImagePath(f.folder.path());
  QSignalSpy spy(f.service.get(), &Wallpapers::currentChanged);
  const QString target = f.file(QStringLiteral("img2.png"));
  f.service->setWallpaper(target);
  EXPECT_EQ(f.state->sets, QStringList{target});
  EXPECT_EQ(f.service->current(), target);
  EXPECT_EQ(spy.count(), 1);
  for (const QVariant& row : f.service->files()) {
    const QVariantMap map = row.toMap();
    EXPECT_EQ(map.value(QStringLiteral("current")).toBool(),
              map.value(QStringLiteral("name")).toString() == QLatin1String("img2.png"));
  }
}

TEST(WallpapersService, IgnoresAPathThatIsNotAFile) {
  Fixture f;
  f.service->setImagePath(f.folder.path());
  f.service->setWallpaper(f.file(QStringLiteral("missing.png")));
  f.service->setWallpaper(f.folder.path());
  EXPECT_TRUE(f.state->sets.isEmpty());
}

TEST(WallpapersService, RandomPicksOneOfTheListedFilesAndNeverRepeatsInARow) {
  Fixture f;
  f.service->setImagePath(f.folder.path());
  f.picks = {1, 1, 2};
  f.service->random();
  ASSERT_EQ(f.state->sets.size(), 1);
  EXPECT_EQ(f.state->sets.first(), f.file(QStringLiteral("img2.png")));
  f.service->random();  // the picker offers the same file again first; the manager asks once more
  ASSERT_EQ(f.state->sets.size(), 2);
  EXPECT_NE(f.state->sets.last(), f.state->sets.first());
}

TEST(WallpapersService, UnavailableWithoutABackendAndInert) {
  Wallpapers::Deps deps;
  deps.realBackend = false;
  Wallpapers service(std::move(deps), nullptr);
  EXPECT_FALSE(service.available());
  service.setWallpaper(QStringLiteral("C:/x.png"));
  service.random();
  service.toggleGallery();
  EXPECT_FALSE(service.galleryOpen());
}

TEST(WallpapersService, ToggleGalleryOpensOnAScreenAndClosesAgain) {
  Fixture f;
  f.service->configure(f.folder.path(), QString(), QStringLiteral("primary"), 0);
  QSignalSpy spy(f.service.get(), &Wallpapers::galleryOpenChanged);
  f.service->toggleGallery();
  EXPECT_TRUE(f.service->galleryOpen());
  EXPECT_EQ(f.service->galleryScreen(), QGuiApplication::primaryScreen()->name());
  EXPECT_EQ(f.service->files().size(), 3);
  f.service->toggleGallery();
  EXPECT_FALSE(f.service->galleryOpen());
  EXPECT_EQ(spy.count(), 2);
}

TEST(WallpapersService, TheHotkeyIsRegisteredOnceAndTogglesTheGallery) {
  Fixture f;
  f.service->configure(f.folder.path(), QStringLiteral("alt+e"), QStringLiteral("cursor"), 0);
  f.service->configure(f.folder.path(), QStringLiteral("alt+e"), QStringLiteral("cursor"), 0);
  ASSERT_EQ(f.hotkeys->attempts.size(), 1u);
  EXPECT_EQ(f.hotkeys->attempts[0].modifiers, 1u);
  EXPECT_EQ(f.hotkeys->attempts[0].virtualKey, static_cast<unsigned>('E'));
  ASSERT_TRUE(f.hotkeys->callback);
  f.hotkeys->callback();
  EXPECT_TRUE(f.service->galleryOpen());
  f.hotkeys->callback();
  EXPECT_FALSE(f.service->galleryOpen());
}

TEST(WallpapersService, ATakenHotkeyOrABadOneDoesNotBreakTheService) {
  Fixture f;
  f.hotkeys->succeed = false;
  f.service->configure(f.folder.path(), QStringLiteral("alt+e"), QString(), 0);
  EXPECT_EQ(f.hotkeys->attempts.size(), 1u);
  f.service->configure(f.folder.path(), QStringLiteral("alt+nonsense"), QString(), 0);
  EXPECT_EQ(f.hotkeys->attempts.size(), 1u);
  EXPECT_EQ(f.service->files().size(), 3);
}

TEST(WallpapersService, ParsesHotkeys) {
  const auto altE = parseHotkey(QStringLiteral(" Alt + E "));
  ASSERT_TRUE(altE);
  EXPECT_EQ(altE->modifiers, 1u);
  EXPECT_EQ(altE->virtualKey, static_cast<unsigned>('E'));
  const auto f5 = parseHotkey(QStringLiteral("ctrl+shift+f5"));
  ASSERT_TRUE(f5);
  EXPECT_EQ(f5->modifiers, 6u);
  EXPECT_EQ(f5->virtualKey, 0x70u + 4);
  EXPECT_EQ(parseHotkey(QStringLiteral("win+space"))->modifiers, 8u);
  EXPECT_FALSE(parseHotkey(QStringLiteral("alt")));
  EXPECT_FALSE(parseHotkey(QStringLiteral("a+b")));
  EXPECT_FALSE(parseHotkey(QStringLiteral("f99")));
  EXPECT_FALSE(parseHotkey(QString()));
}

TEST(WallpapersWidget, TheHotkeyOpensAGalleryWindowWithOneThumbnailPerFile) {
  Fixture f;
  Fixture* fixture = &f;
  FakeHotkeys* hotkeys = f.hotkeys;
  Wallpapers::setFactoryForTests([fixture]() { return std::move(fixture->service); });
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [ Wallpapers { objectName: "wallpapers"; imagePath: "%1"; cornerRadius: 0 } ]
        }
    }
}
)")
              .arg(QDir::fromNativeSeparators(f.folder.path())));
  const bool loaded = h.host->reload();
  Wallpapers::setFactoryForTests({});
  ASSERT_TRUE(loaded) << h.host->error().toStdString();
  ASSERT_TRUE(h.window());
  // No label: the button is hidden, the widget is loaded and owns the hotkey.
  EXPECT_FALSE(findItem(h.window(), QStringLiteral("wallpapers"))->isVisible());
  ASSERT_TRUE(hotkeys->callback);
  hotkeys->callback();
  QQuickItem* gallery = nullptr;
  ASSERT_TRUE(QTest::qWaitFor(
      [&] {
        for (QWindow* window : QGuiApplication::allWindows()) {
          auto* quick = qobject_cast<QQuickWindow*>(window);
          if (quick && quick->isVisible() && quick->contentItem()) {
            gallery = findItem(quick, QStringLiteral("gallery"));
            if (gallery) return true;
          }
        }
        return false;
      },
      3000));
  EXPECT_EQ(gallery->property("count").toInt(), 3);
  hotkeys->callback();
}

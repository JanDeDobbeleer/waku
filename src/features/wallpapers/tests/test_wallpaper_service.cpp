// The wallpapers service behind `wabarc wallpaper ...`: which file a target means and whether it
// is set (on a fake IDesktopWallpaper and temporary folders, so the real wallpaper setter never
// runs), and WallpaperSettings.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QTemporaryDir>
#include <memory>
#include <vector>

#include "features/wallpapers/qml/wallpaper_transition.h"
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
using wabar::wallpapers::resolveWallpaperTarget;
using wabar::wallpapers::Wallpapers;
using wabar::wallpapers::WallpaperTransition;

void makeImage(const QString& path, const QColor& color) {
  QImage image(QSize(40, 20), QImage::Format_RGB32);
  image.fill(color);
  ASSERT_TRUE(image.save(path));
}

void makeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(content);
}

// The transition played by an engine-on change: records what it was asked for and, with no
// WorkerW, skips at once so finished() follows.
struct TransitionLog {
  QStringList played;  // "<path>|<animation>"
};

std::unique_ptr<WallpaperTransition> fakeTransition(const std::shared_ptr<TransitionLog>& log,
                                                    const QString& path, const QString& animation) {
  log->played << path + QLatin1Char('|') + animation;
  WallpaperTransition::Platform platform;
  platform.readSettings = [] { return wabar::wallpapers::DesktopSettings{}; };
  platform.oldImagePath = [] { return QStringLiteral("old"); };
  platform.attach = [](std::uintptr_t, QString* failure) -> std::optional<plat::WorkerWAttachment> {
    if (failure) *failure = QStringLiteral("no WorkerW");
    return std::nullopt;
  };
  platform.decode = [](const QString&, const QString&) {
    QImage image(QSize(8, 8), QImage::Format_RGB32);
    image.fill(Qt::blue);
    return std::make_pair(image, image);
  };
  platform.showWindow = false;
  return std::make_unique<WallpaperTransition>(path, animation, platform, 10);
}

// A wallpaper folder with a.jpg, img2.png, img10.png (Explorer order), notes.txt, fake.png (not
// an image), photo.png and photo.jpg (ambiguous), and an unrelated folder with other.png.
struct Fixture {
  Fixture() {
    EXPECT_TRUE(folder.isValid());
    EXPECT_TRUE(cacheFolder.isValid());
    EXPECT_TRUE(elsewhere.isValid());
    makeImage(inFolder(QStringLiteral("a.jpg")), Qt::blue);
    makeImage(inFolder(QStringLiteral("img2.png")), Qt::green);
    makeImage(inFolder(QStringLiteral("img10.png")), Qt::red);
    makeImage(inFolder(QStringLiteral("Photo.png")), Qt::yellow);
    makeImage(inFolder(QStringLiteral("photo.jpg")), Qt::cyan);
    makeFile(inFolder(QStringLiteral("notes.txt")), "text");
    makeFile(inFolder(QStringLiteral("fake.png")), "this is not a png");
    QDir(folder.path()).mkdir(QStringLiteral("dir.png"));
    makeImage(elsewhere.filePath(QStringLiteral("other.png")), Qt::magenta);
    makeImage(elsewhere.filePath(QStringLiteral("with space.png")), Qt::gray);

    state = std::make_shared<DesktopState>();
    transitions = std::make_shared<TransitionLog>();
    service = make();
  }

  // A new service on the same fake desktop with its own fake hotkeys (`hotkeys` points at the
  // latest). A reload destroys the singleton, so a reload test builds one per generation.
  std::unique_ptr<Wallpapers> make() {
    auto owned = std::make_unique<FakeHotkeys>();
    hotkeys = owned.get();
    Wallpapers::Deps deps;
    deps.backend = [state = state] { return std::make_unique<FakeBackend>(state); };
    deps.picker = [this](int count) { return picks[next++ % picks.size()] % count; };
    deps.cache = std::make_shared<wabar::wallpapers::ThumbnailCache>(cacheFolder.path());
    deps.hotkeys = std::move(owned);
    deps.transition = [log = transitions](const QString& path, const QString& animation) {
      return fakeTransition(log, path, animation);
    };
    deps.realBackend = false;
    return std::make_unique<Wallpapers>(std::move(deps), nullptr);
  }

  QString inFolder(const QString& name) const {
    return QDir::toNativeSeparators(QDir(folder.path()).absoluteFilePath(name));
  }
  QString native(const QString& path) const { return QDir::toNativeSeparators(path); }
  // Folder used as the wallpaper folder; Photo.png/photo.jpg, notes.txt and fake.png are in it.
  void configure() {
    service->configure(QDir::fromNativeSeparators(folder.path()), QString(), QString(), 0);
  }
  QString ok(const QString& path) const { return QStringLiteral("ok: ") + native(path); }
  bool setsEmpty() const { return state->sets.isEmpty(); }

  QTemporaryDir folder;
  QTemporaryDir cacheFolder;
  QTemporaryDir elsewhere;
  std::shared_ptr<DesktopState> state;
  std::shared_ptr<TransitionLog> transitions;
  FakeHotkeys* hotkeys = nullptr;
  std::unique_ptr<Wallpapers> service;
  std::vector<int> picks{0};
  size_t next = 0;
};

// A folder holding only the three plain images, for the wrap-around tests.
struct SmallFolder {
  SmallFolder() {
    EXPECT_TRUE(folder.isValid());
    EXPECT_TRUE(cacheFolder.isValid());
    makeImage(folder.filePath(QStringLiteral("a.jpg")), Qt::blue);
    makeImage(folder.filePath(QStringLiteral("img2.png")), Qt::green);
    makeImage(folder.filePath(QStringLiteral("img10.png")), Qt::red);
    state = std::make_shared<DesktopState>();
    Wallpapers::Deps deps;
    deps.backend = [state = state] { return std::make_unique<FakeBackend>(state); };
    deps.picker = [this](int count) { return pick % count; };
    deps.cache = std::make_shared<wabar::wallpapers::ThumbnailCache>(cacheFolder.path());
    deps.hotkeys = std::make_unique<FakeHotkeys>();
    deps.realBackend = false;
    service = std::make_unique<Wallpapers>(std::move(deps), nullptr);
    service->configure(QDir::fromNativeSeparators(folder.path()), QString(), QString(), 0);
  }
  QString file(const QString& name) const {
    return QDir::toNativeSeparators(folder.filePath(name));
  }
  QString ok(const QString& name) const { return QStringLiteral("ok: ") + file(name); }

  QTemporaryDir folder;
  QTemporaryDir cacheFolder;
  std::shared_ptr<DesktopState> state;
  std::unique_ptr<Wallpapers> service;
  int pick = 0;
};

}  // namespace

// ---- the service: which file, and does it set it ----

TEST(WallpaperService, AFullPathOutsideTheFolderIsSet) {
  Fixture f;
  f.configure();
  const QString path = f.elsewhere.filePath(QStringLiteral("other.png"));
  EXPECT_EQ(f.service->controlSet(path, false), f.ok(path));
  ASSERT_EQ(f.state->sets.size(), 1);
  EXPECT_EQ(QDir::toNativeSeparators(f.state->sets.first()), f.native(path));
}

TEST(WallpaperService, AFullPathWithSpacesAndSlashesOfEitherKindWorks) {
  Fixture f;
  const QString path = f.elsewhere.filePath(QStringLiteral("with space.png"));  // forward slashes
  EXPECT_EQ(f.service->controlSet(path, false), f.ok(path));
  EXPECT_EQ(f.service->controlSet(f.native(path), false), f.ok(path));
}

TEST(WallpaperService, AFileNameIsResolvedInsideTheFolderIgnoringCase) {
  Fixture f;
  f.configure();
  EXPECT_EQ(f.service->controlSet(QStringLiteral("IMG2.PNG"), false),
            f.ok(f.inFolder(QStringLiteral("img2.png"))));
  EXPECT_EQ(f.service->controlSet(QStringLiteral("a.jpg"), false),
            f.ok(f.inFolder(QStringLiteral("a.jpg"))));
}

TEST(WallpaperService, ANameWithoutExtensionTriesTheKnownExtensions) {
  Fixture f;
  f.configure();
  EXPECT_EQ(f.service->controlSet(QStringLiteral("IMG10"), false),
            f.ok(f.inFolder(QStringLiteral("img10.png"))));
  EXPECT_EQ(f.service->controlSet(QStringLiteral("a"), false),
            f.ok(f.inFolder(QStringLiteral("a.jpg"))));
}

TEST(WallpaperService, AnAmbiguousNameIsAnErrorListingTheCandidates) {
  Fixture f;
  f.configure();
  const QString answer = f.service->controlSet(QStringLiteral("photo"), false);
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error: 'photo' is ambiguous")))
      << qPrintable(answer);
  EXPECT_TRUE(answer.contains(f.native(f.inFolder(QStringLiteral("Photo.png")))))
      << qPrintable(answer);
  EXPECT_TRUE(answer.contains(f.native(f.inFolder(QStringLiteral("photo.jpg")))))
      << qPrintable(answer);
  EXPECT_TRUE(f.setsEmpty());
  // The full name is not ambiguous.
  EXPECT_EQ(f.service->controlSet(QStringLiteral("photo.jpg"), true),
            f.ok(f.inFolder(QStringLiteral("photo.jpg"))));
}

TEST(WallpaperService, MissingNonImageAndUnreadableTargetsChangeNothing) {
  Fixture f;
  f.configure();
  const QStringList bad{QStringLiteral("nothing.png"),  // no such name
                        QStringLiteral("nothing"),      // no such name, no extension
                        f.elsewhere.filePath(QStringLiteral("missing.png")),  // no such path
                        QStringLiteral("notes.txt"),  // in the folder, not an image type
                        f.inFolder(QStringLiteral("notes.txt")),  // the same by path
                        QStringLiteral("fake.png"),               // image extension, not decodable
                        f.inFolder(QStringLiteral("dir.png")),    // a folder
                        f.elsewhere.path(),                       // a folder
                        QString()};
  for (const QString& target : bad) {
    const QString answer = f.service->controlSet(target, false);
    EXPECT_TRUE(answer.startsWith(QStringLiteral("error: ")))
        << qPrintable(target) << " -> " << qPrintable(answer);
  }
  EXPECT_TRUE(f.service->controlSet(QStringLiteral("notes.txt"), false)
                  .contains(QStringLiteral("not a supported image type")));
  EXPECT_TRUE(f.service->controlSet(QStringLiteral("fake.png"), false)
                  .contains(QStringLiteral("cannot read")));
  EXPECT_TRUE(f.setsEmpty());
  EXPECT_TRUE(f.transitions->played.isEmpty());
}

TEST(WallpaperService, ANameNeedsAConfiguredFolder) {
  Fixture f;  // not configured
  const QString answer = f.service->controlSet(QStringLiteral("img2.png"), false);
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error: no wallpaper folder")))
      << qPrintable(answer);
}

TEST(WallpaperService, ResolvingIsAPureFunctionOfTargetAndFolder) {
  Fixture f;
  const auto resolved =
      resolveWallpaperTarget(QStringLiteral("Img2"), QDir::fromNativeSeparators(f.folder.path()));
  EXPECT_TRUE(resolved.ok());
  EXPECT_EQ(resolved.path, f.native(f.inFolder(QStringLiteral("img2.png"))));
}

TEST(WallpaperService, DryRunDoesNotCallTheSetter) {
  Fixture f;
  f.configure();
  const QString path = f.inFolder(QStringLiteral("img2.png"));
  EXPECT_EQ(f.service->controlSet(QStringLiteral("img2.png"), true), f.ok(path));
  EXPECT_TRUE(f.setsEmpty());
  EXPECT_EQ(f.service->controlNext(true).left(4), QStringLiteral("ok: "));
  EXPECT_EQ(f.service->controlPrevious(true).left(4), QStringLiteral("ok: "));
  EXPECT_EQ(f.service->controlRandom(true).left(4), QStringLiteral("ok: "));
  EXPECT_TRUE(f.setsEmpty());
  // The same call without the flag does set.
  EXPECT_EQ(f.service->controlSet(QStringLiteral("img2.png"), false), f.ok(path));
  EXPECT_EQ(f.state->sets.size(), 1);
}

TEST(WallpaperService, DryRunWithTheEnginePlaysTheAnimationButNeverSetsIt) {
  Fixture f;
  f.configure();
  f.service->configureEngine(true, QStringLiteral("fade"));
  const QString path = f.inFolder(QStringLiteral("img2.png"));
  EXPECT_EQ(f.service->controlSet(QStringLiteral("img2.png"), true), f.ok(path));
  ASSERT_EQ(f.transitions->played.size(), 1);
  EXPECT_TRUE(f.transitions->played.first().endsWith(QStringLiteral("|fade")));
  QTest::qWait(300);  // the (skipped) transition has finished by now
  EXPECT_TRUE(f.setsEmpty());

  // Without the flag the same flow sets the wallpaper once the animation is done.
  EXPECT_EQ(f.service->controlSet(QStringLiteral("img2.png"), false), f.ok(path));
  EXPECT_TRUE(QTest::qWaitFor([&] { return f.state->sets.size() == 1; }, 3000));
}

TEST(WallpaperService, NextPreviousAndRandomWrapOnTheFileList) {
  SmallFolder f;  // a.jpg, img2.png, img10.png
  // The current wallpaper is not in the folder: next is the first file, previous the last.
  EXPECT_EQ(f.service->controlNext(false), f.ok(QStringLiteral("a.jpg")));
  EXPECT_EQ(f.service->controlNext(false), f.ok(QStringLiteral("img2.png")));
  EXPECT_EQ(f.service->controlNext(false), f.ok(QStringLiteral("img10.png")));
  EXPECT_EQ(f.service->controlNext(false), f.ok(QStringLiteral("a.jpg")));          // wraps
  EXPECT_EQ(f.service->controlPrevious(false), f.ok(QStringLiteral("img10.png")));  // wraps
  EXPECT_EQ(f.service->controlPrevious(false), f.ok(QStringLiteral("img2.png")));
  EXPECT_EQ(f.state->sets.size(), 6);

  // Random never repeats the current one: current img2.png, candidates a.jpg, img10.png.
  f.pick = 1;
  EXPECT_EQ(f.service->controlRandom(false), f.ok(QStringLiteral("img10.png")));
  f.pick = 0;
  EXPECT_EQ(f.service->controlRandom(false), f.ok(QStringLiteral("a.jpg")));
}

TEST(WallpaperService, PreviousFromAnUnknownCurrentIsTheLastFile) {
  SmallFolder f;
  EXPECT_EQ(f.service->controlPrevious(false), f.ok(QStringLiteral("img10.png")));
}

TEST(WallpaperService, AnEmptyFolderAnswersAnError) {
  Fixture f;
  QTemporaryDir empty;
  f.service->configure(QDir::fromNativeSeparators(empty.path()), QString(), QString(), 0);
  EXPECT_TRUE(
      f.service->controlNext(false).startsWith(QStringLiteral("error: no wallpapers found")));
  EXPECT_TRUE(
      f.service->controlRandom(false).startsWith(QStringLiteral("error: no wallpapers found")));
  EXPECT_TRUE(f.setsEmpty());
}

TEST(WallpaperService, GalleryTogglesAndAnswersWhatItDid) {
  Fixture f;
  f.configure();
  EXPECT_EQ(f.service->controlGallery(), QStringLiteral("ok: gallery opened"));
  EXPECT_TRUE(f.service->galleryOpen());
  EXPECT_EQ(f.service->controlGallery(), QStringLiteral("ok: gallery closed"));
  EXPECT_FALSE(f.service->galleryOpen());
}

TEST(WallpaperService, AnUnavailableServiceAnswersErrorsAndSetsNothing) {
  Wallpapers::Deps deps;
  deps.realBackend = false;
  Wallpapers service(std::move(deps), nullptr);
  ASSERT_FALSE(service.available());
  for (const QString& answer :
       {service.controlSet(QStringLiteral("C:\\x.png"), false), service.controlNext(false),
        service.controlPrevious(false), service.controlRandom(false), service.controlGallery()}) {
    EXPECT_TRUE(answer.startsWith(QStringLiteral("error: "))) << qPrintable(answer);
  }
}

// ---- WallpaperSettings ----

namespace {

QString settingsConfig(const QString& folder, const QString& extra = QString()) {
  return QStringLiteral(R"(
import QtQuick
import wabar
import wabar.services

ShellRoot {
    WallpaperSettings { objectName: "settings"; imagePath: "%1"; hotkey: "ctrl+shift+f5"; engine: true; animation: "fade"; %2 }
    Panel { content: PanelLayout { start: [ Text { objectName: "a"; text: "a" } ] } }
}
)")
      .arg(QDir::fromNativeSeparators(folder), extra);
}

}  // namespace

TEST(WallpaperSettingsTest, ConfiguresAndCreatesTheServiceWithoutAWidget) {
  Fixture f;
  Fixture* fixture = &f;
  FakeHotkeys* hotkeys = f.hotkeys;
  int created = 0;
  Wallpapers::setFactoryForTests([fixture, &created]() {
    ++created;
    return std::move(fixture->service);
  });
  Harness h;
  h.start(settingsConfig(f.folder.path()));
  const bool loaded = h.host->reload();
  Wallpapers* service = nullptr;
  if (loaded) {
    service = h.engine.singletonInstance<Wallpapers*>(QStringLiteral("wabar.services"),
                                                      QStringLiteral("Wallpapers"));
  }
  Wallpapers::setFactoryForTests({});
  ASSERT_TRUE(loaded) << h.host->error().toStdString();
  ASSERT_TRUE(service);

  // One service, built by the settings (the validation engine did not build another one).
  EXPECT_EQ(created, 1);
  EXPECT_EQ(QDir::toNativeSeparators(service->imagePath()),
            f.native(QDir::fromNativeSeparators(f.folder.path())));
  ASSERT_EQ(hotkeys->attempts.size(), 1u);
  EXPECT_EQ(hotkeys->attempts.front().modifiers, 2u | 4u);
  EXPECT_EQ(hotkeys->attempts.front().virtualKey, 0x74u);  // VK_F5
  ASSERT_TRUE(hotkeys->callback);

  // The animation and engine reached the service: a dry-run set plays "fade".
  EXPECT_EQ(service->controlSet(QStringLiteral("img2"), true).left(4), QStringLiteral("ok: "));
  ASSERT_EQ(f.transitions->played.size(), 1);
  EXPECT_TRUE(f.transitions->played.first().endsWith(QStringLiteral("|fade")));

  // The hotkey opens the gallery, a window of the settings object.
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
  EXPECT_GE(gallery->property("count").toInt(), 3);
  EXPECT_EQ(service->controlGallery(), QStringLiteral("ok: gallery closed"));
}

TEST(WallpaperSettingsTest, TheWindowsStillBuildAndThePartIsKeptByTheShell) {
  Fixture f;
  Fixture* fixture = &f;
  Wallpapers::setFactoryForTests([fixture]() { return std::move(fixture->service); });
  Harness h;
  h.start(settingsConfig(f.folder.path()));
  const bool loaded = h.host->reload();
  Wallpapers::setFactoryForTests({});
  ASSERT_TRUE(loaded) << h.host->error().toStdString();
  ASSERT_TRUE(h.host->shell());
  EXPECT_EQ(h.host->windows().size(), 1);
  QObject* settings = h.host->shell()->findChild<QObject*>(QStringLiteral("settings"));
  ASSERT_NE(settings, nullptr);
  EXPECT_EQ(settings->parent(), h.host->shell());
  EXPECT_TRUE(settings->property("active").toBool());
  EXPECT_EQ(h.text(QStringLiteral("a")), QStringLiteral("a"));
}

TEST(WallpaperSettingsTest, AReloadReplacesTheServiceAndConfiguresTheNewOne) {
  Fixture f;
  Fixture* fixture = &f;
  // A reload destroys the singleton and creates it again, so every generation gets its own.
  Wallpapers::setFactoryForTests([fixture]() { return fixture->make(); });
  Harness h;
  h.start(settingsConfig(f.folder.path()));
  ASSERT_TRUE(h.host->reload());
  auto current = [&] {
    return h.engine.singletonInstance<Wallpapers*>(QStringLiteral("wabar.services"),
                                                   QStringLiteral("Wallpapers"));
  };
  QPointer<Wallpapers> first = current();
  ASSERT_TRUE(first);
  EXPECT_FALSE(first->imagePath().isEmpty());
  EXPECT_EQ(f.hotkeys->attempts.size(), 1u);

  write(h.shellFile(), settingsConfig(f.elsewhere.path()));
  ASSERT_TRUE(h.host->reload());
  QPointer<Wallpapers> second = current();
  ASSERT_TRUE(second);
  EXPECT_FALSE(first);  // the old service (and its hotkey) went with the singleton
  EXPECT_EQ(QDir::toNativeSeparators(second->imagePath()),
            f.native(QDir::fromNativeSeparators(f.elsewhere.path())));
  EXPECT_EQ(f.hotkeys->attempts.size(), 1u);  // the new service registered its own
  EXPECT_TRUE(f.hotkeys->callback);

  write(h.shellFile(), shellConfig());  // no settings any more
  ASSERT_TRUE(h.host->reload());
  Wallpapers* third = current();
  Wallpapers::setFactoryForTests({});
  ASSERT_TRUE(third);
  EXPECT_FALSE(second);
  EXPECT_TRUE(third->imagePath().isEmpty());
  EXPECT_FALSE(f.hotkeys->callback);
}

TEST(WallpaperSettingsTest, AShellWithoutWindowsLoadsToo) {
  Fixture f;
  Fixture* fixture = &f;
  Wallpapers::setFactoryForTests([fixture]() { return std::move(fixture->service); });
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import wabar.services
ShellRoot { WallpaperSettings { imagePath: "%1" } }
)")
              .arg(QDir::fromNativeSeparators(f.folder.path())));
  const bool loaded = h.host->reload();
  Wallpapers::setFactoryForTests({});
  EXPECT_TRUE(loaded) << h.host->error().toStdString();
}

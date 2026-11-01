// The lock screen picture that follows a wallpaper change: when the service hands it to the
// setter (a fake, so no test touches the real lock screen or wallpaper), and the pure checks of
// which files qualify.
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "features/wallpapers/lock_screen.h"
#include "features/wallpapers/qml/wallpaper_transition.h"
#include "features/wallpapers/qml/wallpapers_service.h"
#include "features/wallpapers/testing/fake_lock_screen.h"
#include "features/wallpapers/testing/fake_wallpaper_backend.h"

namespace {

namespace wp = wabar::wallpapers;
using wp::Wallpapers;

QStringList& lockScreenMessages() {
  static QStringList messages;
  return messages;
}

QtMessageHandler g_previousHandler = nullptr;

void collectLockScreenMessages(QtMsgType type, const QMessageLogContext& context,
                               const QString& text) {
  if (text.contains(QStringLiteral("lock screen"))) {
    lockScreenMessages().append(text);
    return;
  }
  if (g_previousHandler) g_previousHandler(type, context, text);
}

// Collects every log line that mentions the lock screen while it lives.
struct LockScreenMessageCapture {
  LockScreenMessageCapture() {
    lockScreenMessages().clear();
    g_previousHandler = qInstallMessageHandler(collectLockScreenMessages);
  }
  ~LockScreenMessageCapture() { qInstallMessageHandler(g_previousHandler); }
  bool any(const QString& part) const {
    for (const QString& line : lockScreenMessages()) {
      if (line.contains(part)) return true;
    }
    return false;
  }
};

void makeImage(const QString& path) {
  QImage image(QSize(40, 20), QImage::Format_RGB32);
  image.fill(Qt::blue);
  ASSERT_TRUE(image.save(path));
}

void makeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(content);
}

// A 1x1 GIF with two frames.
QByteArray animatedGif() {
  // Signature, 1x1 screen with a 2-colour table, the table.
  const QByteArray header = QByteArray::fromHex(
      "474946383961"
      "01000100800000"
      "000000ffffff");
  // Graphic control (10 cs delay), image descriptor, LZW data.
  const QByteArray frame = QByteArray::fromHex(
      "21f904000a000000"
      "2c000000000100010000"
      "0202440100");
  return header + frame + frame + QByteArray::fromHex("3b");
}

std::unique_ptr<wp::WallpaperTransition> skippingTransition(const QString& path,
                                                            const QString& animation) {
  wp::WallpaperTransition::Platform platform;
  platform.readSettings = [] { return wp::DesktopSettings{}; };
  platform.oldImagePath = [] { return QStringLiteral("old"); };
  platform.attach = [](std::uintptr_t, QString* failure) -> std::optional<wp::WorkerWAttachment> {
    if (failure) *failure = QStringLiteral("no WorkerW");
    return std::nullopt;
  };
  platform.decode = [](const QString&, const QString&) {
    QImage image(QSize(8, 8), QImage::Format_RGB32);
    image.fill(Qt::blue);
    return std::make_pair(image, image);
  };
  platform.showWindow = false;
  return std::make_unique<wp::WallpaperTransition>(path, animation, platform, 10);
}

// A folder with a.jpg, b.png, clip.mp4 and anim.gif, a fake desktop and a fake lock screen.
struct LockFixture {
  explicit LockFixture(bool dryRun = false) {
    EXPECT_TRUE(folder.isValid());
    makeImage(file(QStringLiteral("a.jpg")));
    makeImage(file(QStringLiteral("b.png")));
    makeFile(file(QStringLiteral("clip.mp4")), "not really a video");
    makeFile(file(QStringLiteral("anim.gif")), animatedGif());
    desktop = std::make_shared<wp::DesktopState>();
    lock = std::make_shared<wp::LockScreenLog>();
    Wallpapers::Deps deps;
    deps.backend = [state = desktop] { return std::make_unique<wp::FakeBackend>(state); };
    deps.picker = [n = 0](int count) mutable { return n++ % count; };
    deps.transition = skippingTransition;
    deps.lockScreen = std::make_unique<wp::FakeLockScreen>(lock);
    deps.realBackend = false;
    deps.dryRun = dryRun;
    service = std::make_unique<Wallpapers>(std::move(deps), nullptr);
    service->configure(QDir::fromNativeSeparators(folder.path()), QString(), QString(), 0);
  }
  QString file(const QString& name) const {
    return QDir::toNativeSeparators(QDir(folder.path()).absoluteFilePath(name));
  }

  QTemporaryDir folder;
  std::shared_ptr<wp::DesktopState> desktop;
  std::shared_ptr<wp::LockScreenLog> lock;
  std::unique_ptr<Wallpapers> service;
};

}  // namespace

TEST(WallpaperLockScreen, FollowsEveryChangeByDefault) {
  LockFixture f;
  EXPECT_TRUE(f.service->setLockScreen());
  EXPECT_EQ(f.service->controlSet(QStringLiteral("a.jpg"), false).left(4), QStringLiteral("ok: "));
  EXPECT_EQ(f.lock->sets, QStringList{f.file(QStringLiteral("a.jpg"))});
  f.service->setWallpaper(f.file(QStringLiteral("b.png")));
  EXPECT_EQ(f.lock->sets,
            (QStringList{f.file(QStringLiteral("a.jpg")), f.file(QStringLiteral("b.png"))}));
  EXPECT_EQ(f.desktop->sets.size(), 2);
}

TEST(WallpaperLockScreen, SwitchedOffItIsNeverCalled) {
  LockFixture f;
  QSignalSpy changed(f.service.get(), &Wallpapers::setLockScreenChanged);
  f.service->setSetLockScreen(false);
  f.service->setSetLockScreen(false);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_FALSE(f.service->setLockScreen());
  f.service->setWallpaper(f.file(QStringLiteral("a.jpg")));
  f.service->random();
  EXPECT_EQ(f.desktop->sets.size(), 2);
  EXPECT_TRUE(f.lock->sets.isEmpty());
}

TEST(WallpaperLockScreen, RandomWithoutTheEngineFollowsToo) {
  LockFixture f;
  f.service->random();
  ASSERT_EQ(f.desktop->sets.size(), 1);
  ASSERT_EQ(f.lock->sets.size(), 1);
  EXPECT_EQ(QDir::toNativeSeparators(f.lock->sets.first()),
            QDir::toNativeSeparators(f.desktop->sets.first()));
}

TEST(WallpaperLockScreen, DryRunOnlyLogs) {
  LockScreenMessageCapture capture;
  LockFixture f(true);
  f.service->setWallpaper(f.file(QStringLiteral("a.jpg")));
  f.service->random();
  EXPECT_TRUE(f.desktop->sets.isEmpty());
  EXPECT_TRUE(f.lock->sets.isEmpty());
  EXPECT_TRUE(
      capture.any(QStringLiteral("lock screen (dry run): ") + f.file(QStringLiteral("a.jpg"))))
      << qPrintable(lockScreenMessages().join(QLatin1Char('\n')));
}

TEST(WallpaperLockScreen, ADryRunCallOnlyLogs) {
  LockScreenMessageCapture capture;
  LockFixture f;
  EXPECT_EQ(f.service->controlSet(QStringLiteral("b.png"), true).left(4), QStringLiteral("ok: "));
  EXPECT_TRUE(f.lock->sets.isEmpty());
  EXPECT_TRUE(
      capture.any(QStringLiteral("lock screen (dry run): ") + f.file(QStringLiteral("b.png"))));
}

TEST(WallpaperLockScreen, MissingFilesVideosAndAnimationsAreNotSent) {
  LockFixture f;
  EXPECT_TRUE(f.service->controlSet(f.file(QStringLiteral("gone.png")), false)
                  .startsWith(QStringLiteral("error: ")));
  f.service->setWallpaper(f.file(QStringLiteral("clip.mp4")));
  f.service->setWallpaper(f.file(QStringLiteral("anim.gif")));
  EXPECT_EQ(f.desktop->sets.size(), 2);  // the fake desktop takes anything
  EXPECT_TRUE(f.lock->sets.isEmpty());
}

TEST(WallpaperLockScreen, AFailureIsLoggedAndTheWallpaperStillChanges) {
  LockScreenMessageCapture capture;
  LockFixture f;
  f.lock->fail = true;
  EXPECT_EQ(f.service->controlSet(QStringLiteral("a.jpg"), false),
            QStringLiteral("ok: ") + f.file(QStringLiteral("a.jpg")));
  EXPECT_EQ(f.desktop->sets.size(), 1);
  EXPECT_EQ(f.service->current(), f.desktop->sets.first());
  EXPECT_EQ(f.lock->sets.size(), 1);
  EXPECT_TRUE(capture.any(QStringLiteral("fake lock screen failure")))
      << qPrintable(lockScreenMessages().join(QLatin1Char('\n')));
}

TEST(WallpaperLockScreen, WithTheEngineItFollowsTheCommit) {
  LockFixture f;
  f.service->configureEngine(true, QStringLiteral("fade"));
  f.service->setWallpaper(f.file(QStringLiteral("b.png")));
  EXPECT_TRUE(QTest::qWaitFor([&] { return f.lock->sets.size() == 1; }, 3000));
  EXPECT_EQ(f.desktop->sets.size(), 1);
  EXPECT_EQ(f.lock->sets.first(), f.file(QStringLiteral("b.png")));
}

// The auto-change timer (one second, the shortest interval) takes the path of random().
void startAutoChange(LockFixture& f) {
  f.service->configure(QDir::fromNativeSeparators(f.folder.path()), QString(), QString(), 1);
}
void stopAutoChange(LockFixture& f) {
  f.service->configure(QDir::fromNativeSeparators(f.folder.path()), QString(), QString(), 0);
}

TEST(WallpaperLockScreen, ATimedChangeFollowsOnce) {
  LockFixture f;
  startAutoChange(f);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !f.lock->sets.isEmpty(); }, 5000));
  stopAutoChange(f);
  ASSERT_EQ(f.desktop->sets.size(), 1);
  ASSERT_EQ(f.lock->sets.size(), 1);
  // The fixture's picker takes the first file of the folder.
  EXPECT_EQ(f.lock->sets.first(), f.file(QStringLiteral("a.jpg")));
  EXPECT_EQ(QDir::toNativeSeparators(f.desktop->sets.first()), f.file(QStringLiteral("a.jpg")));
}

TEST(WallpaperLockScreen, ATimedChangeLeavesItAloneWhenSwitchedOff) {
  LockFixture f;
  f.service->setSetLockScreen(false);
  startAutoChange(f);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !f.desktop->sets.isEmpty(); }, 5000));
  stopAutoChange(f);
  EXPECT_TRUE(f.lock->sets.isEmpty());
}

TEST(WallpaperLockScreen, ATimedChangeInADryRunOnlyLogs) {
  LockScreenMessageCapture capture;
  LockFixture f(true);
  startAutoChange(f);
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return capture.any(QStringLiteral("lock screen (dry run): ")); }, 5000))
      << qPrintable(lockScreenMessages().join(QLatin1Char('\n')));
  stopAutoChange(f);
  EXPECT_TRUE(f.desktop->sets.isEmpty());
  EXPECT_TRUE(f.lock->sets.isEmpty());
}

TEST(WallpaperLockScreen, WhichFilesQualifyAndWhichNeedACopy) {
  QTemporaryDir folder;
  ASSERT_TRUE(folder.isValid());
  const QString still = folder.filePath(QStringLiteral("still.png"));
  const QString clip = folder.filePath(QStringLiteral("clip.webm"));
  const QString anim = folder.filePath(QStringLiteral("anim.gif"));
  makeImage(still);
  makeFile(clip, "x");
  makeFile(anim, animatedGif());
  EXPECT_FALSE(wp::lockScreenSkipReason(still).has_value());
  EXPECT_EQ(wp::lockScreenSkipReason(folder.filePath(QStringLiteral("none.png"))),
            QStringLiteral("no such file"));
  EXPECT_EQ(wp::lockScreenSkipReason(folder.path()), QStringLiteral("no such file"));
  EXPECT_EQ(wp::lockScreenSkipReason(clip), QStringLiteral("a video"));
  EXPECT_EQ(wp::lockScreenSkipReason(anim), QStringLiteral("an animated image"));

  for (const char* name : {"a.jpg", "a.JPEG", "a.png", "a.bmp"}) {
    EXPECT_FALSE(wp::lockScreenNeedsCopy(QString::fromLatin1(name))) << name;
  }
  for (const char* name : {"a.webp", "a.tif", "a.tiff", "a.gif", "a"}) {
    EXPECT_TRUE(wp::lockScreenNeedsCopy(QString::fromLatin1(name))) << name;
  }
}

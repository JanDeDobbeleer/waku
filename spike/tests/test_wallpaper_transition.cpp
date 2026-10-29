// The Qt Quick wallpaper transition and its wiring in the Wallpapers service. Nothing real runs:
// the attach, the registry reads and the image decode are fakes, the window is never shown, the
// backend is a fake IDesktopWallpaper and the images are tiny generated files in a temp folder.
#include <gtest/gtest.h>

#include <QDir>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <vector>

#include "spike/services/wallpaper_transition.h"
#include "spike/services/wallpapers_service.h"

namespace {

namespace plat = yasb::platform;
using wabar::services::Wallpapers;
using wabar::services::WallpaperTransition;

constexpr QRgb kOld = 0xFFFF0000;  // red
constexpr QRgb kNew = 0xFF0000FF;  // blue

QImage solid(QRgb color, QSize size = QSize(40, 20)) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(color);
  return image;
}

struct FakeCalls {
  int attaches = 0;
  int decodes = 0;
};

// Two 100x100 monitors side by side in a 200x100 WorkerW, Fill mode, black background.
WallpaperTransition::Platform fakePlatform(std::shared_ptr<FakeCalls> calls, bool attachOk = true,
                                           bool readable = true) {
  WallpaperTransition::Platform platform;
  platform.readSettings = [] { return yasb::widgets::DesktopSettings{}; };
  platform.oldImagePath = [] { return QStringLiteral("old"); };
  platform.attach = [calls, attachOk](std::uintptr_t,
                                      QString* failure) -> std::optional<plat::WorkerWAttachment> {
    ++calls->attaches;
    if (!attachOk) {
      if (failure) *failure = QStringLiteral("no WorkerW");
      return std::nullopt;
    }
    plat::WorkerWAttachment attachment;
    attachment.workerRect = QRect(0, 0, 200, 100);
    attachment.monitors = {QRect(0, 0, 100, 100), QRect(100, 0, 100, 100)};
    return attachment;
  };
  platform.decode = [calls, readable](const QString&, const QString&) {
    ++calls->decodes;
    return readable ? std::make_pair(solid(kNew), solid(kOld))
                    : std::make_pair(QImage(), solid(kOld));
  };
  platform.showWindow = false;
  return platform;
}

std::unique_ptr<WallpaperTransition> make(const QString& animation, int ms = 20,
                                          bool attachOk = true) {
  return std::make_unique<WallpaperTransition>(
      QStringLiteral("new"), animation, fakePlatform(std::make_shared<FakeCalls>(), attachOk), ms);
}

QRgb pixel(const QImage& image, int x, int y) { return image.pixel(x, y) | 0xFF000000u; }

}  // namespace

TEST(WallpaperTransition, ProgressZeroShowsTheOldImageAndOneTheNew) {
  for (const char* name : {"split", "circle", "diamond", "slide_top", "fade"}) {
    auto t = make(QString::fromLatin1(name));
    t->onImagesLoaded(solid(kNew), solid(kOld));
    ASSERT_EQ(t->state(), WallpaperTransition::State::Playing) << name;
    t->setProgress(0.0);
    QImage frame = t->renderFrame();
    ASSERT_EQ(frame.size(), QSize(200, 100)) << name;
    // circle and diamond reveal a 1 px sliver at the centre first; the corners are untouched.
    EXPECT_EQ(pixel(frame, 5, 5), kOld) << name;
    EXPECT_EQ(pixel(frame, 150, 90), kOld) << name;
    t->setProgress(1.0);
    frame = t->renderFrame();
    EXPECT_EQ(pixel(frame, 5, 5), kNew) << name;
    EXPECT_EQ(pixel(frame, 195, 95), kNew) << name;
  }
}

TEST(WallpaperTransition, AreasFollowTheMonitorsOfTheAttachment) {
  auto t = make(QStringLiteral("split"));
  t->onImagesLoaded(solid(kNew), solid(kOld));
  ASSERT_EQ(t->areas().size(), 2u);
  EXPECT_EQ(t->areas()[0].x, 0);
  EXPECT_EQ(t->areas()[1].x, 100);
  EXPECT_EQ(t->areas()[1].w, 100);
  EXPECT_EQ(t->scaledNew().size(), 2u);
  EXPECT_EQ(t->scaledOld().size(), 2u);
  // Fill: the 40x20 picture covers a 100x100 monitor, scaled to 200x100.
  EXPECT_EQ(t->scaledNew()[0].pixmap.size(), QSize(200, 100));
}

TEST(WallpaperTransition, SplitRevealsBandsFromTheTopAndBottomEdges) {
  auto t = make(QStringLiteral("split"));
  t->onImagesLoaded(solid(kNew), solid(kOld));
  t->setProgress(0.5);
  const QImage frame = t->renderFrame();
  // 100 px tall monitor, t = 0.5: a band of 25 px at each edge, on both monitors.
  for (int x : {10, 150}) {
    EXPECT_EQ(pixel(frame, x, 0), kNew);
    EXPECT_EQ(pixel(frame, x, 24), kNew);
    EXPECT_EQ(pixel(frame, x, 25), kOld);
    EXPECT_EQ(pixel(frame, x, 74), kOld);
    EXPECT_EQ(pixel(frame, x, 75), kNew);
    EXPECT_EQ(pixel(frame, x, 99), kNew);
  }
}

TEST(WallpaperTransition, FadeBlendsAndAnUnknownNameFades) {
  auto t = make(QStringLiteral("wobble"));
  t->onImagesLoaded(solid(kNew), solid(kOld));
  t->setProgress(0.5);
  const QRgb mixed = t->renderFrame().pixel(50, 50);
  EXPECT_GT(qRed(mixed), 100);
  EXPECT_LT(qRed(mixed), 160);
  EXPECT_GT(qBlue(mixed), 100);
  EXPECT_LT(qBlue(mixed), 160);
}

TEST(WallpaperTransition, AnUnreadableImageSkipsTheAnimation) {
  auto calls = std::make_shared<FakeCalls>();
  WallpaperTransition t(QStringLiteral("new"), QStringLiteral("split"), fakePlatform(calls), 20);
  QSignalSpy skipped(&t, &WallpaperTransition::skipped);
  QSignalSpy finished(&t, &WallpaperTransition::finished);
  t.onImagesLoaded(QImage(), solid(kOld));
  EXPECT_EQ(skipped.count(), 1);
  EXPECT_EQ(finished.count(), 1);
  EXPECT_EQ(t.state(), WallpaperTransition::State::Skipped);
  EXPECT_FALSE(t.windowVisible());
  EXPECT_EQ(calls->attaches, 0);
}

TEST(WallpaperTransition, NoWorkerWSkipsTheAnimationAndSaysWhy) {
  auto t = make(QStringLiteral("split"), 20, false);
  QSignalSpy skipped(t.get(), &WallpaperTransition::skipped);
  QSignalSpy finished(t.get(), &WallpaperTransition::finished);
  t->onImagesLoaded(solid(kNew), solid(kOld));
  ASSERT_EQ(skipped.count(), 1);
  EXPECT_EQ(skipped.first().first().toString(), QStringLiteral("no WorkerW"));
  EXPECT_EQ(finished.count(), 1);
  EXPECT_EQ(t->state(), WallpaperTransition::State::Skipped);
}

TEST(WallpaperTransition, PlaysToTheEndAndFinishesOnce) {
  auto t = make(QStringLiteral("split"), 30);
  QSignalSpy finished(t.get(), &WallpaperTransition::finished);
  t->onImagesLoaded(solid(kNew), solid(kOld));
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() > 0; }, 3000));
  QTest::qWait(60);
  EXPECT_EQ(finished.count(), 1);
  EXPECT_DOUBLE_EQ(t->progress(), 1.0);
  EXPECT_EQ(t->state(), WallpaperTransition::State::Finished);
}

TEST(WallpaperTransition, CancelStopsSilently) {
  auto t = make(QStringLiteral("split"), 40);
  QSignalSpy finished(t.get(), &WallpaperTransition::finished);
  t->onImagesLoaded(solid(kNew), solid(kOld));
  t->cancel();
  QTest::qWait(120);
  EXPECT_EQ(finished.count(), 0);
  EXPECT_EQ(t->state(), WallpaperTransition::State::Cancelled);
  EXPECT_FALSE(t->windowVisible());
}

TEST(WallpaperTransition, StartDecodesOffTheGuiThreadAndPlays) {
  auto calls = std::make_shared<FakeCalls>();
  auto platform = fakePlatform(calls);
  const auto guiThread = QThread::currentThread();
  auto decodeThread = std::make_shared<QThread*>(nullptr);
  platform.decode = [decodeThread](const QString&, const QString&) {
    *decodeThread = QThread::currentThread();
    return std::make_pair(solid(kNew), solid(kOld));
  };
  WallpaperTransition t(QStringLiteral("new"), QStringLiteral("split"), platform, 20);
  QSignalSpy finished(&t, &WallpaperTransition::finished);
  t.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() > 0; }, 3000));
  EXPECT_NE(*decodeThread, guiThread);
  EXPECT_EQ(calls->attaches, 1);
}

TEST(WallpaperTransition, DestroyingWhileDecodingIsSafe) {
  auto platform = fakePlatform(std::make_shared<FakeCalls>());
  platform.decode = [](const QString&, const QString&) {
    QThread::msleep(40);
    return std::make_pair(solid(kNew), solid(kOld));
  };
  {
    WallpaperTransition t(QStringLiteral("new"), QStringLiteral("split"), platform, 20);
    t.start();
  }
  QTest::qWait(120);  // the late result finds no transition
}

// --- the service

namespace {

struct DesktopState {
  QString wallpaper;
  QStringList sets;
};

class FakeBackend final : public plat::DesktopWallpaperBackend {
 public:
  explicit FakeBackend(std::shared_ptr<DesktopState> state) : state_(std::move(state)) {}
  bool setWallpaper(const QString&, const QString& path, QString*) override {
    state_->wallpaper = path;
    state_->sets << path;
    return true;
  }
  std::vector<plat::WallpaperMonitor> monitors() override {
    return {{QStringLiteral("mon-1"), QRect(0, 0, 100, 100)}};
  }
  QString wallpaperOf(const QString&) override { return state_->wallpaper; }

 private:
  std::shared_ptr<DesktopState> state_;
};

struct ServiceFixture {
  explicit ServiceFixture(bool attachOk = true, bool readable = true, bool dryRun = false) {
    EXPECT_TRUE(folder.isValid());
    EXPECT_TRUE(cacheFolder.isValid());
    for (const char* name : {"a.png", "b.png"})
      EXPECT_TRUE(solid(kNew).save(folder.filePath(QString::fromLatin1(name))));
    state = std::make_shared<DesktopState>();
    Wallpapers::Deps deps;
    deps.backend = [state = state] { return std::make_unique<FakeBackend>(state); };
    deps.picker = [](int) { return 0; };
    deps.cache = std::make_shared<yasb::widgets::ThumbnailCache>(cacheFolder.path());
    deps.hotkeys = std::make_unique<NoHotkeys>();
    deps.realBackend = false;
    deps.dryRun = dryRun;
    deps.transition = [this, attachOk, readable](const QString& image, const QString& animation) {
      images << image;
      animations << animation;
      return std::make_unique<WallpaperTransition>(
          image, animation, fakePlatform(std::make_shared<FakeCalls>(), attachOk, readable), 30);
    };
    service = std::make_unique<Wallpapers>(std::move(deps), nullptr);
    service->setImagePath(folder.path());
  }

  struct NoHotkeys final : wabar::services::HotkeyHost {
    bool registerKey(const wabar::services::HotkeyCombo&, std::function<void()>) override {
      return true;
    }
    void unregisterKey() override {}
  };

  QString file(const char* name) const {
    return QDir::toNativeSeparators(
        QDir(folder.path()).absoluteFilePath(QString::fromLatin1(name)));
  }

  QTemporaryDir folder;
  QTemporaryDir cacheFolder;
  std::shared_ptr<DesktopState> state;
  std::unique_ptr<Wallpapers> service;
  QStringList images;
  QStringList animations;
};

}  // namespace

TEST(WallpapersEngine, OffByDefaultSetsAtOnceWithoutATransition) {
  ServiceFixture f;
  f.service->setWallpaper(f.file("a.png"));
  EXPECT_EQ(f.state->sets, QStringList{f.file("a.png")});
  EXPECT_TRUE(f.images.isEmpty());
}

TEST(WallpapersEngine, SetsTheWallpaperExactlyOnceWhenTheAnimationEnds) {
  ServiceFixture f;
  f.service->configureEngine(true, QStringLiteral("split"));
  f.service->setWallpaper(f.file("a.png"));
  EXPECT_TRUE(f.state->sets.isEmpty());  // nothing before the animation is over
  ASSERT_TRUE(QTest::qWaitFor([&] { return !f.state->sets.isEmpty(); }, 3000));
  QTest::qWait(100);
  EXPECT_EQ(f.state->sets, QStringList{f.file("a.png")});
  EXPECT_EQ(f.animations, QStringList{QStringLiteral("split")});
}

TEST(WallpapersEngine, ANewChangeCancelsTheRunningOne) {
  ServiceFixture f;
  f.service->configureEngine(true, QStringLiteral("circle"));
  f.service->setWallpaper(f.file("a.png"));
  f.service->setWallpaper(f.file("b.png"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !f.state->sets.isEmpty(); }, 3000));
  QTest::qWait(150);
  EXPECT_EQ(f.state->sets, QStringList{f.file("b.png")});
  EXPECT_EQ(f.images.size(), 2);
}

TEST(WallpapersEngine, AnUnreadableImageOrNoWorkerWSetsTheWallpaperAtOnce) {
  for (int variant = 0; variant < 2; ++variant) {
    ServiceFixture f(/*attachOk=*/variant != 0, /*readable=*/variant != 1);
    f.service->configureEngine(true, QStringLiteral("split"));
    f.service->setWallpaper(f.file("a.png"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !f.state->sets.isEmpty(); }, 3000)) << variant;
    EXPECT_EQ(f.state->sets, QStringList{f.file("a.png")}) << variant;
  }
}

TEST(WallpapersEngine, DryRunPlaysTheTransitionButNeverSetsTheWallpaper) {
  ServiceFixture f(true, true, /*dryRun=*/true);
  f.service->configureEngine(true, QStringLiteral("split"));
  f.service->setWallpaper(f.file("a.png"));
  QTest::qWait(250);
  EXPECT_EQ(f.images.size(), 1);
  EXPECT_TRUE(f.state->sets.isEmpty());
}

TEST(WallpapersEngine, RandomAnimatesWhenTheEngineIsOn) {
  ServiceFixture f;
  f.service->configureEngine(true, QStringLiteral("diamond"));
  f.service->random();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !f.state->sets.isEmpty(); }, 3000));
  EXPECT_EQ(f.images.size(), 1);
  EXPECT_EQ(f.animations, QStringList{QStringLiteral("diamond")});
}

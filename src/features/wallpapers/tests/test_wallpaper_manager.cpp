// WallpaperManager, the file scan and the thumbnail cache against a fake desktop: no widget, no
// engine (the animated change is wabar's WallpaperTransition), nothing touches the real wallpaper.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "features/wallpapers/desktop_wallpaper.h"
#include "features/wallpapers/thumbnail_cache.h"
#include "features/wallpapers/wallpaper_files.h"
#include "features/wallpapers/wallpaper_manager.h"

using nlohmann::json;
using namespace wabar::wallpapers;
using wabar::wallpapers::DesktopWallpaperBackend;
using wabar::wallpapers::WallpaperMonitor;

namespace {

void touch(const QString& path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("x");
}

QString writePng(const QString& path, QColor color, int w = 64, int h = 36) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QImage image(w, h, QImage::Format_RGB32);
  image.fill(color);
  EXPECT_TRUE(image.save(path, "PNG"));
  return path;
}

QString nativeAbsolute(const QString& path) {
  return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

// The fake desktop: records every set and answers the monitor questions.
struct FakeDesktop {
  struct Set {
    QString monitor;
    QString path;
  };
  std::vector<Set> sets;
  std::vector<WallpaperMonitor> monitors{{QStringLiteral("MON1"), QRect(0, 0, 800, 600)}};
  QHash<QString, QString> current;
  bool failSet = false;
  int backendsCreated = 0;
};

class FakeBackend final : public DesktopWallpaperBackend {
 public:
  explicit FakeBackend(std::shared_ptr<FakeDesktop> desktop) : desktop_(std::move(desktop)) {}
  bool setWallpaper(const QString& monitorId, const QString& path, QString* error) override {
    if (desktop_->failSet) {
      if (error) *error = QStringLiteral("fake failure");
      return false;
    }
    desktop_->sets.push_back({monitorId, path});
    return true;
  }
  std::vector<WallpaperMonitor> monitors() override { return desktop_->monitors; }
  QString wallpaperOf(const QString& id) override { return desktop_->current.value(id); }

 private:
  std::shared_ptr<FakeDesktop> desktop_;
};

// A manager wired to the fake desktop: it never reaches the real wallpaper or the shell.
struct ManagerRig {
  std::shared_ptr<FakeDesktop> desktop = std::make_shared<FakeDesktop>();
  std::vector<QString> commands;
  std::vector<std::function<void(const QString&)>> pendingCommands;
  bool deferCommands = false;
  std::vector<int> picks;  // consumed in order; the last one repeats
  std::unique_ptr<WallpaperManager> manager;

  ManagerRig() {
    WallpaperManager::Dependencies deps;
    deps.backend = [this] {
      ++desktop->backendsCreated;
      return std::make_unique<FakeBackend>(desktop);
    };
    deps.runner = [this](const QString& command, std::function<void(const QString&)> done) {
      commands.push_back(command);
      if (deferCommands)
        pendingCommands.push_back(std::move(done));
      else
        done(QString());
    };
    deps.picker = [this](int count) {
      const int pick = picks.empty() ? 0 : picks.front();
      if (picks.size() > 1) picks.erase(picks.begin());
      return std::min(pick, count - 1);
    };
    manager = std::make_unique<WallpaperManager>(std::move(deps));
  }
};

}  // namespace

class WallpaperManagerTest : public testing::Test {
 protected:
  ManagerRig rig_;
  QTemporaryDir temp_;
  std::shared_ptr<ThumbnailCache> cache_ =
      std::make_shared<ThumbnailCache>(QDir(temp_.path()).filePath(QStringLiteral("thumbs")));

  QStringList makeImages(int count) {
    QStringList files;
    for (int i = 0; i < count; ++i) {
      files << writePng(QDir(temp_.path()).filePath(QStringLiteral("img%1.png").arg(i + 1)),
                        QColor((40 * i) % 255, 80, 120));
    }
    return files;
  }
};

// --- files and cache --------------------------------------------------------------------------

TEST_F(WallpaperManagerTest, ImagePathAcceptsStringOrList) {
  EXPECT_EQ(imagePathsFrom(json("C:/a")), QStringList{QStringLiteral("C:/a")});
  EXPECT_EQ(imagePathsFrom(json::array({"C:/a", "D:/b"})),
            (QStringList{QStringLiteral("C:/a"), QStringLiteral("D:/b")}));
  EXPECT_TRUE(imagePathsFrom(json(nullptr)).isEmpty());
}

TEST_F(WallpaperManagerTest, NaturalOrderPutsImg2BeforeImg10) {
  for (const char* name : {"img10.png", "img2.png", "img1.png"})
    touch(QDir(temp_.path()).filePath(QString::fromLatin1(name)));
  QStringList names;
  for (const QString& file : collectImageFiles({temp_.path()}, supportedImageTypes({0, 0})))
    names << QFileInfo(file).fileName();
  EXPECT_EQ(names, (QStringList{QStringLiteral("img1.png"), QStringLiteral("img2.png"),
                                QStringLiteral("img10.png")}));
}

TEST_F(WallpaperManagerTest, MissingFolderYieldsNoFiles) {
  EXPECT_TRUE(collectImageFiles({QDir(temp_.path()).filePath(QStringLiteral("nope"))},
                                {QStringLiteral("png")})
                  .isEmpty());
}

TEST_F(WallpaperManagerTest, ThumbnailCacheRoundTripsAndKeysFollowSizeAndFile) {
  const QString image = writePng(QDir(temp_.path()).filePath(QStringLiteral("a.png")), Qt::red);
  const QString key = cache_->key(image, 100, 50);
  ASSERT_FALSE(key.isEmpty());
  EXPECT_EQ(key, cache_->key(image, 100, 50));
  EXPECT_NE(key, cache_->key(image, 101, 50));
  EXPECT_TRUE(cache_->key(QDir(temp_.path()).filePath(QStringLiteral("gone.png")), 1, 1).isEmpty());
  EXPECT_FALSE(cache_->load(key).has_value());

  QImage thumb(8, 4, QImage::Format_ARGB32);
  thumb.fill(Qt::blue);
  cache_->save(key, thumb);
  const auto loaded = cache_->load(key);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->size(), QSize(8, 4));
}

TEST_F(WallpaperManagerTest, ThumbnailCachePrunesExpiredFiles) {
  QImage thumb(2, 2, QImage::Format_ARGB32);
  thumb.fill(Qt::green);
  cache_->save(QStringLiteral("old.png"), thumb);
  cache_->save(QStringLiteral("new.png"), thumb);
  QFile old(QDir(cache_->folder()).filePath(QStringLiteral("old.png")));
  ASSERT_TRUE(old.open(QIODevice::ReadWrite));
  const QDateTime ancient =
      QDateTime::currentDateTime().addSecs(-ThumbnailCache::kMaxAgeSeconds - 3600);
  ASSERT_TRUE(old.setFileTime(ancient, QFileDevice::FileModificationTime));
  old.close();

  cache_->prune();
  EXPECT_FALSE(cache_->load(QStringLiteral("old.png")).has_value());
  EXPECT_TRUE(cache_->load(QStringLiteral("new.png")).has_value());
}

// --- manager ----------------------------------------------------------------------------------

TEST_F(WallpaperManagerTest, ChangeBackgroundNeverRepeatsAndSetsEveryMonitor) {
  const QStringList files = makeImages(3);
  WallpaperManager& manager = *rig_.manager;
  manager.configure({temp_.path()}, 60, false, {});

  rig_.picks = {0};
  manager.changeBackground();
  ASSERT_EQ(rig_.desktop->sets.size(), 1u);
  EXPECT_TRUE(rig_.desktop->sets[0].monitor.isEmpty());  // all monitors
  EXPECT_EQ(rig_.desktop->sets[0].path, nativeAbsolute(files[0]));

  // The picker keeps answering 0 first; the manager must re-pick until the image differs.
  rig_.picks = {0, 0, 2};
  manager.changeBackground();
  ASSERT_EQ(rig_.desktop->sets.size(), 2u);
  EXPECT_EQ(rig_.desktop->sets[1].path, nativeAbsolute(files[2]));
}

TEST_F(WallpaperManagerTest, ChangeBackgroundWithoutImagesTouchesNothing) {
  rig_.manager->configure({QDir(temp_.path()).filePath(QStringLiteral("empty"))}, 60, false, {});
  rig_.manager->changeBackground();
  EXPECT_TRUE(rig_.desktop->sets.empty());
  EXPECT_EQ(rig_.desktop->backendsCreated, 0);
  EXPECT_FALSE(rig_.manager->isRunning());
}

TEST_F(WallpaperManagerTest, RunAfterReplacesImageAndBlocksOverlappingChanges) {
  const QStringList files = makeImages(2);
  rig_.deferCommands = true;
  rig_.manager->configure({temp_.path()}, 60, false,
                          {QStringLiteral("tool {image} --now"), QStringLiteral("second")});

  rig_.picks = {1};
  rig_.manager->changeBackground();
  EXPECT_TRUE(rig_.manager->isRunning());
  ASSERT_EQ(rig_.commands.size(), 1u);
  EXPECT_EQ(rig_.commands[0], QStringLiteral("tool \"%1\" --now").arg(nativeAbsolute(files[1])));
  EXPECT_EQ(rig_.manager->lastImage(), nativeAbsolute(files[1]));

  rig_.manager->changeBackground();  // ignored: a change is still running
  EXPECT_EQ(rig_.desktop->sets.size(), 1u);

  rig_.pendingCommands.front()(QString());
  ASSERT_EQ(rig_.commands.size(), 2u);
  EXPECT_TRUE(rig_.manager->isRunning());
  rig_.pendingCommands.back()(QString());
  EXPECT_FALSE(rig_.manager->isRunning());
}

TEST_F(WallpaperManagerTest, SetFailureResetsTheGuardAndSkipsCommands) {
  makeImages(1);
  rig_.desktop->failSet = true;
  rig_.manager->configure({temp_.path()}, 60, false, {QStringLiteral("never")});
  rig_.manager->changeBackground();
  EXPECT_FALSE(rig_.manager->isRunning());
  EXPECT_TRUE(rig_.commands.empty());
}

TEST_F(WallpaperManagerTest, TimerFollowsChangeAutomatically) {
  WallpaperManager& manager = *rig_.manager;
  manager.configure({temp_.path()}, 90, true, {});
  EXPECT_TRUE(manager.timerRunning());
  EXPECT_EQ(manager.timerIntervalMs(), 90000);
  manager.configure({temp_.path()}, 90, false, {});
  EXPECT_FALSE(manager.timerRunning());
}

TEST_F(WallpaperManagerTest, MonitorIdsComeFromTheBackend) {
  rig_.desktop->monitors = {{QStringLiteral("A"), {}}, {QStringLiteral("B"), {}}};
  EXPECT_EQ(rig_.manager->monitorIds(), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
}

TEST_F(WallpaperManagerTest, SettingOneMonitorTargetsThatMonitor) {
  const QString image = makeImages(1).front();
  rig_.manager->configure({temp_.path()}, 60, false, {});
  rig_.manager->setWallpaper(image, QStringLiteral("MON1"));
  ASSERT_EQ(rig_.desktop->sets.size(), 1u);
  EXPECT_EQ(rig_.desktop->sets[0].monitor, QStringLiteral("MON1"));
  EXPECT_EQ(rig_.desktop->sets[0].path, nativeAbsolute(image));
}

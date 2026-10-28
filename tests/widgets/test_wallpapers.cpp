#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QTest>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

#include "core/bar/theme_class.h"
#include "core/widgets/event_bus.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/registry.h"
#include "widgets/wallpapers/gallery.h"
#include "widgets/wallpapers/gallery_layout.h"
#include "widgets/wallpapers/thumbnail_cache.h"
#include "widgets/wallpapers/wallpaper_engine.h"
#include "widgets/wallpapers/wallpaper_files.h"
#include "widgets/wallpapers/wallpaper_layout.h"
#include "widgets/wallpapers/wallpaper_manager.h"
#include "widgets/wallpapers/wallpapers_widget.h"

using nlohmann::json;
using namespace yasb::widgets;
using yasb::platform::DesktopWallpaperBackend;
using yasb::platform::WallpaperFit;
using yasb::platform::WallpaperMonitor;

namespace {

// --- helpers ----------------------------------------------------------------------------------

bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!done()) {
    if (timer.elapsed() > timeoutMs) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return true;
}

QString qs(const std::string& text) { return QString::fromStdString(text); }

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

// Counts the messages that contain a marker; installed for the length of one test.
struct MessageCounter {
  static inline int count = 0;
  static inline QString marker;
  static inline QtMessageHandler previous = nullptr;
  explicit MessageCounter(const QString& text) {
    count = 0;
    marker = text;
    previous = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext& context, const QString& message) {
          if (message.contains(marker)) ++count;
          if (previous) previous(type, context, message);
        });
  }
  ~MessageCounter() { qInstallMessageHandler(previous); }
  MessageCounter(const MessageCounter&) = delete;
  MessageCounter& operator=(const MessageCounter&) = delete;
};

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

// A manager wired to the fake desktop: it never reaches the real wallpaper, the shell or WorkerW.
struct ManagerRig {
  std::shared_ptr<FakeDesktop> desktop = std::make_shared<FakeDesktop>();
  std::vector<QString> commands;
  std::vector<std::function<void(const QString&)>> pendingCommands;
  bool deferCommands = false;
  std::vector<int> picks;  // consumed in order; the last one repeats
  std::function<WallpaperEngine*(const QString&, const QString&)> engine;
  std::unique_ptr<WallpaperManager> manager;

  ManagerRig() {
    WallpaperManager::Dependencies deps;
    deps.backend = [this] {
      ++desktop->backendsCreated;
      return std::make_unique<FakeBackend>(desktop);
    };
    deps.engine = [this](const QString& image, const QString& animation) -> WallpaperEngine* {
      return engine ? engine(image, animation) : nullptr;
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

WallpaperEngine::Platform fakePlatform(const QString& oldImage, bool workerFound,
                                       std::shared_ptr<int> attaches = nullptr) {
  WallpaperEngine::Platform platform;
  platform.readSettings = [] { return DesktopSettings{}; };
  platform.transcodedPath = [oldImage] { return oldImage; };
  platform.attach = [workerFound, attaches](
                        std::uintptr_t,
                        QString* failure) -> std::optional<yasb::platform::WorkerWAttachment> {
    if (attaches) ++*attaches;
    if (!workerFound) {
      if (failure) *failure = QStringLiteral("Could not locate WorkerW");
      return std::nullopt;
    }
    yasb::platform::WorkerWAttachment attachment;
    attachment.workerRect = QRect(0, 0, 800, 600);
    attachment.monitors = {QRect(0, 0, 800, 600)};
    return attachment;
  };
  return platform;
}

// --- vector replay ----------------------------------------------------------------------------

json loadVectors() {
  std::ifstream in(std::filesystem::path(YASB_VECTORS_DIR) / "labels" / "wallpapers.json",
                   std::ios::binary);
  return json::parse(in);
}

QStringList toList(const json& values) {
  QStringList list;
  for (const json& v : values) list << qs(v.get<std::string>());
  return list;
}

WallpaperFit fitFrom(const std::string& name) {
  if (name == "fit") return WallpaperFit::Fit;
  if (name == "stretch") return WallpaperFit::Stretch;
  if (name == "tile") return WallpaperFit::Tile;
  if (name == "center") return WallpaperFit::Center;
  if (name == "span") return WallpaperFit::Span;
  return WallpaperFit::Fill;
}

void expectPlacement(const Placement& actual, const json& expected) {
  EXPECT_NEAR(actual.x, expected["x"].get<double>(), 1e-9);
  EXPECT_NEAR(actual.scale, expected["scale"].get<double>(), 1e-9);
  EXPECT_NEAR(actual.shear, expected["shear"].get<double>(), 1e-9);
  EXPECT_NEAR(actual.opacity, expected["opacity"].get<double>(), 1e-9);
  EXPECT_NEAR(actual.focus, expected["focus"].get<double>(), 1e-9);
}

void replaySort(const json& spec, const json& expected) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  for (const json& name : spec["files"]) touch(dir.filePath(qs(name.get<std::string>())));
  const QStringList types = supportedImageTypes({0, 0});
  QStringList actual;
  for (const QString& file : collectImageFiles({dir.path()}, types))
    actual << QDir(dir.path()).relativeFilePath(file);
  EXPECT_EQ(actual, toList(expected["order"]));
}

void replayBuilds(const json& spec, const json& expected) {
  ASSERT_EQ(spec["builds"].size(), expected["types"].size());
  for (size_t i = 0; i < spec["builds"].size(); ++i) {
    SCOPED_TRACE(i);
    const auto& build = spec["builds"][i];
    EXPECT_EQ(supportedImageTypes({build[0].get<int>(), build[1].get<int>()}),
              toList(expected["types"][i]));
  }
}

void replayFind(const json& spec, const json& expected) {
  QHash<QString, QString> wallpapers;
  for (const auto& [name, path] : spec["wallpapers"].items())
    wallpapers.insert(qs(name), qs(path.get<std::string>()));
  const QHash<QString, int> found = findWallpapers(toList(spec["files"]), wallpapers);
  EXPECT_EQ(static_cast<size_t>(found.size()), expected["found"].size());
  for (const auto& [name, index] : expected["found"].items())
    EXPECT_EQ(found.value(qs(name), -1), index.get<int>()) << name;
}

std::optional<std::pair<int, int>> limitsFrom(const json& value) {
  if (value.is_null()) return std::nullopt;
  return std::make_pair(value[0].get<int>(), value[1].get<int>());
}

void replayTranscode(const json& spec, const json& expected) {
  const auto limits = limitsFrom(spec["limits"]);
  for (size_t i = 0; i < spec["sizes"].size(); ++i) {
    const QSize size =
        transcodeSize(spec["sizes"][i][0].get<int>(), spec["sizes"][i][1].get<int>(), limits);
    EXPECT_EQ(size.width(), expected["sizes"][i][0].get<int>()) << i;
    EXPECT_EQ(size.height(), expected["sizes"][i][1].get<int>()) << i;
  }
}

void replayLayout(const json& spec, const json& expected) {
  DesktopSettings settings;
  settings.fit = fitFrom(spec["fit"].get<std::string>());
  settings.transcodeLimits = limitsFrom(spec.value("limits", json(nullptr)));
  settings.panoramaThreshold = spec.value("panorama", 2.22);
  const double dpr = spec.value("dpr", 1.0);
  std::vector<ScreenArea> areas;
  for (const json& a : spec["areas"])
    areas.push_back({a[0].get<int>(), a[1].get<int>(), a[2].get<int>(), a[3].get<int>(), dpr});
  const QPixmap source(spec["image"][0].get<int>(), spec["image"][1].get<int>());
  const auto images = layoutForScreens(source, areas, dpr, settings, QSize());
  ASSERT_EQ(images.size(), expected["images"].size());
  for (size_t i = 0; i < images.size(); ++i) {
    const json& want = expected["images"][i];
    EXPECT_EQ(images[i].pixmap.width(), want[0].get<int>()) << i;
    EXPECT_EQ(images[i].pixmap.height(), want[1].get<int>()) << i;
    EXPECT_EQ(images[i].offsetX, want[2].get<int>()) << i;
    EXPECT_EQ(images[i].offsetY, want[3].get<int>()) << i;
  }
}

void replayReveal(const json& spec, const json& expected) {
  const auto& rect = spec["rect"];
  const int x = rect[0].get<int>();
  const int y = rect[1].get<int>();
  const int w = rect[2].get<int>();
  const int h = rect[3].get<int>();
  const double fractions[6][2] = {{0.5, 0.5},   {0.05, 0.05}, {0.95, 0.05},
                                  {0.05, 0.95}, {0.95, 0.95}, {0.5, 0.02}};
  for (size_t i = 0; i < expected["frames"].size(); ++i) {
    const json& frame = expected["frames"][i];
    const double t = frame["t"].get<double>();
    SCOPED_TRACE(t);
    const QPainterPath path = revealPath(qs(spec["animation"].get<std::string>()), x, y, w, h, t);
    const QRectF box = path.boundingRect();
    EXPECT_NEAR(box.x(), frame["box"][0].get<double>(), 0.01);
    EXPECT_NEAR(box.y(), frame["box"][1].get<double>(), 0.01);
    EXPECT_NEAR(box.width(), frame["box"][2].get<double>(), 0.01);
    EXPECT_NEAR(box.height(), frame["box"][3].get<double>(), 0.01);
    for (size_t s = 0; s < 6; ++s) {
      EXPECT_EQ(path.contains(QPointF(x + w * fractions[s][0], y + h * fractions[s][1])),
                frame["samples"][s].get<bool>())
          << "sample " << s;
    }
  }
}

void replayCards(const json& spec, const json& expected) {
  const auto style = makeCardStyle(qs(spec["style"].get<std::string>()));
  const int width = spec["width"].get<int>();
  const QSize card = cardSizeFor(width, qs(spec["orientation"].get<std::string>()));
  const QSize view(spec["view"][0].get<int>(), spec["view"][1].get<int>());
  const int count = spec["count"].get<int>();

  const FitResult fit = fitToView(view.width(), style->stepPixels(width), count);
  EXPECT_EQ(fit.neighbours, expected["neighbours"].get<int>());
  EXPECT_EQ(fit.wraps, expected["wraps"].get<bool>());
  EXPECT_NEAR(style->maxScale(), expected["max_scale"].get<double>(), 1e-9);
  EXPECT_NEAR(style->stepPixels(width) / width, expected["step"].get<double>(), 1e-9);
  EXPECT_EQ(pageStepFor(view.width(), width, style->stepPixels(width)),
            expected["page_step"].get<int>());

  const Placement selected = style->selectedSpot(width);
  expectPlacement(selected, expected["selected"]);
  const QRectF area = projectCard(selected, card, view, style->border()).boundingRect();
  EXPECT_NEAR(area.width(), expected["thumbnail"][0].get<double>(), 1e-9);
  EXPECT_NEAR(area.height(), expected["thumbnail"][1].get<double>(), 1e-9);
  const QPolygonF polygon = projectCard(selected, card, view);
  ASSERT_EQ(polygon.size(), 4);
  for (size_t i = 0; i < 4; ++i) {
    const QPointF point = polygon.at(static_cast<qsizetype>(i));
    EXPECT_NEAR(point.x(), expected["polygon"][i][0].get<double>(), 1e-9);
    EXPECT_NEAR(point.y(), expected["polygon"][i][1].get<double>(), 1e-9);
  }

  for (const json& frame : expected["frames"]) {
    const double offset = frame["offset"].get<double>();
    SCOPED_TRACE(offset);
    const auto visible = visibleCards(offset, count, fit.neighbours, fit.wraps);
    ASSERT_EQ(visible.size(), frame["visible"].size());
    for (size_t i = 0; i < visible.size(); ++i) {
      EXPECT_EQ(visible[i].index, frame["visible"][i][0].get<int>());
      EXPECT_NEAR(visible[i].distance, frame["visible"][i][1].get<double>(), 1e-9);
    }
    const auto placed = style->layoutCards(visible, width);
    ASSERT_EQ(placed.size(), frame["cards"].size());
    for (size_t i = 0; i < placed.size(); ++i) {
      EXPECT_EQ(placed[i].index, frame["cards"][i]["index"].get<int>());
      expectPlacement(placed[i].spot, frame["cards"][i]);
    }
    EXPECT_NEAR(normaliseOffset(offset, count, fit.wraps), frame["normalised"].get<double>(), 1e-9);
  }
}

}  // namespace

class WallpapersTest : public testing::Test {
 protected:
  EventBus eventBus_;
  yasb::bar::GlobalState globalState_;
  PollingWorkerRegistry pollingWorkers_;
  ManagerRig rig_;
  QTemporaryDir temp_;
  std::shared_ptr<ThumbnailCache> cache_ =
      std::make_shared<ThumbnailCache>(QDir(temp_.path()).filePath(QStringLiteral("thumbs")));
  QString lastMode_;

  WidgetContext context(const json& normalized) {
    TypedWidgetOptions typed = normalized.get<yasb::config::options::WallpapersConfig>();
    return {"wallpapers",
            "yasb.wallpapers.WallpapersWidget",
            normalized,
            {},
            {},
            eventBus_,
            globalState_,
            pollingWorkers_,
            std::move(typed)};
  }

  // Test seams: the fake manager, a scanner that returns `files` with index 1 as current, and a
  // screen picker that records the keybinding mode.
  WallpapersWidget::Hooks hooks(const QStringList& files = {}) {
    WallpapersWidget::Hooks h;
    h.manager = rig_.manager.get();
    h.cache = cache_;
    h.scanner = [files](const QStringList&, const std::vector<GalleryScreen>& screens) {
      ScanResult result;
      result.files = files;
      for (const GalleryScreen& screen : screens) result.current.insert(screen.name, 1);
      return result;
    };
    h.screenForMode = [this](const QString& mode) {
      lastMode_ = mode;
      return QGuiApplication::primaryScreen();
    };
    return h;
  }

  json options() {
    return {
        {"label", ""},
        {"update_interval", 60},
        {"change_automatically", false},
        {"image_path", temp_.path().toStdString()},
        {"tooltip", true},
        {"run_after", json::array()},
        {"engine", {{"enabled", false}, {"animation", "split"}}},
        {"gallery",
         {{"type", "strip"},
          {"image_width", 120},
          {"accent_color", "#fff"},
          {"orientation", "portrait"},
          {"image_corner_radius", 0}}},
        {"keybindings", {{{"keys", "alt+e"}, {"action", "toggle_gallery"}, {"screen", "cursor"}}}},
        {"callbacks",
         {{"on_left", "toggle_gallery"},
          {"on_middle", "do_nothing"},
          {"on_right", "change_wallpaper"}}}};
  }

  QStringList makeImages(int count) {
    QStringList files;
    for (int i = 0; i < count; ++i) {
      files << writePng(QDir(temp_.path()).filePath(QStringLiteral("img%1.png").arg(i + 1)),
                        QColor((40 * i) % 255, 80, 120));
    }
    return files;
  }
};

// --- Python parity ----------------------------------------------------------------------------

TEST_F(WallpapersTest, ReplaysPythonVectors) {
  const json vectors = loadVectors();
  ASSERT_FALSE(vectors.empty());
  using Replay = void (*)(const json&, const json&);
  const std::map<std::string, Replay> replays = {
      {"sort", replaySort},           {"builds", replayBuilds}, {"find", replayFind},
      {"transcode", replayTranscode}, {"layout", replayLayout}, {"reveal", replayReveal},
      {"cards", replayCards}};
  int steps = 0;
  for (const json& scenario : vectors) {
    SCOPED_TRACE(scenario["scenario"].get<std::string>());
    WallpapersWidget widget(context(scenario["normalized_options"]), hooks());
    widget.show();
    const auto& initial = scenario["initial"];
    ASSERT_EQ(widget.labels().size(), initial["primary"].size());
    for (size_t i = 0; i < widget.labels().size(); ++i) {
      EXPECT_EQ(widget.labels()[i]->text().toStdString(),
                initial["primary"][i]["text"].get<std::string>());
      EXPECT_EQ(widget.labels()[i]->property("class").toString().toStdString(),
                initial["primary"][i]["class"].get<std::string>());
      EXPECT_EQ(widget.labels()[i]->isHidden(), initial["primary"][i]["hidden"].get<bool>());
    }
    EXPECT_EQ(widget.labelsAlt().size(), initial["alt"].size());
    EXPECT_EQ(widget.widgetFrame()->property("class").toString().toStdString(),
              initial["frame_class"].get<std::string>());
    EXPECT_EQ(widget.labelContainer()->property("class").toString().toStdString(),
              initial["container_class"].get<std::string>());

    for (const json& step : scenario["steps"]) {
      for (const auto& [name, replay] : replays) {
        if (!step["step"].contains(name)) continue;
        SCOPED_TRACE(name);
        replay(step["step"][name], step["snapshot"]["computed"]);
        ++steps;
      }
    }
  }
  EXPECT_GT(steps, 60);
}

// --- files and cache --------------------------------------------------------------------------

TEST_F(WallpapersTest, ImagePathAcceptsStringOrList) {
  EXPECT_EQ(imagePathsFrom(json("C:/a")), QStringList{QStringLiteral("C:/a")});
  EXPECT_EQ(imagePathsFrom(json::array({"C:/a", "D:/b"})),
            (QStringList{QStringLiteral("C:/a"), QStringLiteral("D:/b")}));
  EXPECT_TRUE(imagePathsFrom(json(nullptr)).isEmpty());
}

TEST_F(WallpapersTest, NaturalOrderPutsImg2BeforeImg10) {
  for (const char* name : {"img10.png", "img2.png", "img1.png"})
    touch(QDir(temp_.path()).filePath(QString::fromLatin1(name)));
  QStringList names;
  for (const QString& file : collectImageFiles({temp_.path()}, supportedImageTypes({0, 0})))
    names << QFileInfo(file).fileName();
  EXPECT_EQ(names, (QStringList{QStringLiteral("img1.png"), QStringLiteral("img2.png"),
                                QStringLiteral("img10.png")}));
}

TEST_F(WallpapersTest, MissingFolderYieldsNoFiles) {
  EXPECT_TRUE(collectImageFiles({QDir(temp_.path()).filePath(QStringLiteral("nope"))},
                                {QStringLiteral("png")})
                  .isEmpty());
}

TEST_F(WallpapersTest, ThumbnailCacheRoundTripsAndKeysFollowSizeAndFile) {
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

TEST_F(WallpapersTest, ThumbnailCachePrunesExpiredFiles) {
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

TEST_F(WallpapersTest, ChangeBackgroundNeverRepeatsAndSetsEveryMonitor) {
  const QStringList files = makeImages(3);
  WallpaperManager& manager = *rig_.manager;
  manager.configure({temp_.path()}, 60, false, {}, {});

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

TEST_F(WallpapersTest, ChangeBackgroundWithoutImagesTouchesNothing) {
  rig_.manager->configure({QDir(temp_.path()).filePath(QStringLiteral("empty"))}, 60, false, {},
                          {});
  rig_.manager->changeBackground();
  EXPECT_TRUE(rig_.desktop->sets.empty());
  EXPECT_EQ(rig_.desktop->backendsCreated, 0);
  EXPECT_FALSE(rig_.manager->isRunning());
}

TEST_F(WallpapersTest, RunAfterReplacesImageAndBlocksOverlappingChanges) {
  const QStringList files = makeImages(2);
  rig_.deferCommands = true;
  rig_.manager->configure({temp_.path()}, 60, false,
                          {QStringLiteral("tool {image} --now"), QStringLiteral("second")}, {});

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

TEST_F(WallpapersTest, SetFailureResetsTheGuardAndSkipsCommands) {
  makeImages(1);
  rig_.desktop->failSet = true;
  rig_.manager->configure({temp_.path()}, 60, false, {QStringLiteral("never")}, {});
  rig_.manager->changeBackground();
  EXPECT_FALSE(rig_.manager->isRunning());
  EXPECT_TRUE(rig_.commands.empty());
}

TEST_F(WallpapersTest, SingleMonitorSetNeverAnimates) {
  const QString image = makeImages(1).front();
  int engines = 0;
  rig_.engine = [&engines](const QString&, const QString&) -> WallpaperEngine* {
    ++engines;
    return nullptr;
  };
  rig_.manager->configure({temp_.path()}, 60, false, {}, {true, QStringLiteral("split")});
  rig_.manager->setWallpaper(image, QStringLiteral("MON1"), true);
  EXPECT_EQ(engines, 0);
  ASSERT_EQ(rig_.desktop->sets.size(), 1u);
  EXPECT_EQ(rig_.desktop->sets[0].monitor, QStringLiteral("MON1"));
}

TEST_F(WallpapersTest, TimerFollowsChangeAutomatically) {
  WallpaperManager& manager = *rig_.manager;
  manager.configure({temp_.path()}, 90, true, {}, {});
  EXPECT_TRUE(manager.timerRunning());
  EXPECT_EQ(manager.timerIntervalMs(), 90000);
  manager.configure({temp_.path()}, 90, false, {}, {});
  EXPECT_FALSE(manager.timerRunning());
}

TEST_F(WallpapersTest, MonitorIdsComeFromTheBackend) {
  rig_.desktop->monitors = {{QStringLiteral("A"), {}}, {QStringLiteral("B"), {}}};
  EXPECT_EQ(rig_.manager->monitorIds(), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
}

// --- engine -----------------------------------------------------------------------------------

TEST_F(WallpapersTest, MissingWorkerWSetsWithoutAnimationAndLogsOnce) {
  const QString fresh = writePng(QDir(temp_.path()).filePath(QStringLiteral("new.png")), Qt::red);
  const QString old = writePng(QDir(temp_.path()).filePath(QStringLiteral("old.png")), Qt::blue);
  auto attaches = std::make_shared<int>(0);
  rig_.engine = [&](const QString& image, const QString& animation) {
    return new WallpaperEngine(image, animation, fakePlatform(old, false, attaches), 20);
  };
  rig_.manager->configure({temp_.path()}, 60, false, {}, {true, QStringLiteral("split")});
  MessageCounter counter(QStringLiteral("Wallpaper animation skipped"));

  rig_.manager->setWallpaper(fresh);
  ASSERT_TRUE(waitFor([&] { return rig_.desktop->sets.size() == 1; }));
  rig_.manager->setWallpaper(fresh);
  ASSERT_TRUE(waitFor([&] { return rig_.desktop->sets.size() == 2; }));
  QCoreApplication::processEvents();

  EXPECT_EQ(*attaches, 2);
  EXPECT_EQ(MessageCounter::count, 1);
  EXPECT_EQ(rig_.desktop->sets[0].path, nativeAbsolute(fresh));
}

TEST_F(WallpapersTest, UnreadableOldWallpaperAlsoSkipsTheAnimation) {
  const QString fresh = writePng(QDir(temp_.path()).filePath(QStringLiteral("new.png")), Qt::red);
  auto attaches = std::make_shared<int>(0);
  const QString missing = QDir(temp_.path()).filePath(QStringLiteral("none.png"));
  rig_.engine = [&](const QString& image, const QString& animation) {
    return new WallpaperEngine(image, animation, fakePlatform(missing, true, attaches), 20);
  };
  rig_.manager->configure({temp_.path()}, 60, false, {}, {true, QStringLiteral("circle")});
  rig_.manager->setWallpaper(fresh);
  ASSERT_TRUE(waitFor([&] { return rig_.desktop->sets.size() == 1; }));
  EXPECT_EQ(*attaches, 0);
}

TEST_F(WallpapersTest, SplitTransitionPlaysThenSetsTheWallpaper) {
  const QString fresh =
      writePng(QDir(temp_.path()).filePath(QStringLiteral("new.png")), Qt::red, 400, 300);
  const QString old =
      writePng(QDir(temp_.path()).filePath(QStringLiteral("old.png")), Qt::blue, 400, 300);
  QPointer<WallpaperEngine> created;
  rig_.engine = [&](const QString& image, const QString& animation) {
    created = new WallpaperEngine(image, animation, fakePlatform(old, true), 40);
    return created.data();
  };
  rig_.manager->configure({temp_.path()}, 60, false, {}, {true, QStringLiteral("split")});
  rig_.manager->setWallpaper(fresh);

  ASSERT_TRUE(waitFor([&] { return created && created->animated(); }));
  EXPECT_TRUE(rig_.desktop->sets.empty());  // nothing committed while the overlay plays
  EXPECT_EQ(created->areas().size(), 1u);
  EXPECT_EQ(created->scaledNew().size(), 1u);
  ASSERT_TRUE(waitFor([&] { return rig_.desktop->sets.size() == 1; }));
  ASSERT_TRUE(created);
  EXPECT_DOUBLE_EQ(created->progress(), 1.0);
  EXPECT_TRUE(rig_.desktop->sets[0].monitor.isEmpty());
}

// --- gallery ----------------------------------------------------------------------------------

TEST_F(WallpapersTest, GalleryOpensOnTheChosenScreenAtTheCurrentWallpaper) {
  const QStringList files = makeImages(5);
  WallpapersWidget widget(context(options()), hooks(files));
  widget.show();

  widget.toggleGallery();
  WallpaperGallery* gallery = widget.gallery();
  ASSERT_NE(gallery, nullptr);
  EXPECT_EQ(lastMode_, QStringLiteral("cursor"));  // the toggle_gallery keybinding's screen
  EXPECT_TRUE(gallery->isVisible());
  EXPECT_EQ(gallery->screenName(), QGuiApplication::primaryScreen()->name());

  ASSERT_TRUE(waitFor([&] { return !gallery->scanning(); }));
  EXPECT_EQ(gallery->imageFiles(), files);
  EXPECT_EQ(gallery->view()->index(), 1);  // the scanner reported index 1 as current
  EXPECT_EQ(gallery->accent(), QColor(QStringLiteral("#fff")));
  EXPECT_EQ(gallery->cardSize(), QSize(120, 213));
  EXPECT_EQ(gallery->geometry().width(), QGuiApplication::primaryScreen()->geometry().width());
  ASSERT_TRUE(waitFor([&] { return gallery->hasPixmap(1); }));
}

TEST_F(WallpapersTest, GalleryKeysNavigateAndEnterApplies) {
  const QStringList files = makeImages(6);
  WallpapersWidget widget(context(options()), hooks(files));
  widget.show();
  widget.toggleGallery();
  QPointer<WallpaperGallery> gallery = widget.gallery();
  ASSERT_TRUE(waitFor([&] { return !gallery->scanning(); }));

  QTest::keyClick(gallery, Qt::Key_Right);
  EXPECT_EQ(gallery->view()->index(), 2);
  QTest::keyClick(gallery, Qt::Key_End);
  EXPECT_EQ(gallery->view()->index(), 5);
  QTest::keyClick(gallery, Qt::Key_Home);
  EXPECT_EQ(gallery->view()->index(), 0);

  QTest::keyClick(gallery, Qt::Key_Return);
  EXPECT_TRUE(gallery->isClosing());
  ASSERT_EQ(rig_.desktop->sets.size(), 1u);
  EXPECT_EQ(rig_.desktop->sets[0].path, nativeAbsolute(files[0]));
  ASSERT_TRUE(waitFor([&] { return gallery.isNull(); }));
}

TEST_F(WallpapersTest, GalleryEscapeClosesWithoutChangingTheWallpaper) {
  const QStringList files = makeImages(3);
  WallpapersWidget widget(context(options()), hooks(files));
  widget.show();
  widget.toggleGallery();
  QPointer<WallpaperGallery> gallery = widget.gallery();
  ASSERT_TRUE(waitFor([&] { return !gallery->scanning(); }));
  QTest::keyClick(gallery, Qt::Key_Escape);
  ASSERT_TRUE(waitFor([&] { return gallery.isNull(); }));
  EXPECT_TRUE(rig_.desktop->sets.empty());
}

TEST_F(WallpapersTest, SecondToggleClosesTheGallery) {
  const QStringList files = makeImages(3);
  WallpapersWidget widget(context(options()), hooks(files));
  widget.show();
  EXPECT_TRUE(widget.runCallback(QStringLiteral("toggle_gallery")));
  QPointer<WallpaperGallery> gallery = widget.gallery();
  ASSERT_FALSE(gallery.isNull());
  EXPECT_TRUE(widget.runCallback(QStringLiteral("toggle_gallery")));
  EXPECT_TRUE(gallery->isClosing());
  ASSERT_TRUE(waitFor([&] { return gallery.isNull(); }));
}

TEST_F(WallpapersTest, GalleryWithNoWallpapersHasNothingToApply) {
  WallpapersWidget widget(context(options()), hooks());
  widget.show();
  widget.toggleGallery();
  WallpaperGallery* gallery = widget.gallery();
  ASSERT_TRUE(waitFor([&] { return !gallery->scanning(); }));
  EXPECT_TRUE(gallery->imageFiles().isEmpty());
  EXPECT_FALSE(gallery->selectedImage().has_value());
  QTest::keyClick(gallery, Qt::Key_Return);  // nothing to apply
  EXPECT_TRUE(rig_.desktop->sets.empty());
  EXPECT_FALSE(gallery->isClosing());
}

// --- widget -----------------------------------------------------------------------------------

TEST_F(WallpapersTest, EmptyLabelKeepsTheWidgetLoadedForTheHotkey) {
  WallpapersWidget widget(context(options()), hooks());
  EXPECT_TRUE(widget.labels().empty());
  EXPECT_EQ(widget.widgetFrame()->property("class").toString(),
            QStringLiteral("widget wallpapers-widget"));
  EXPECT_TRUE(widget.callbacks().contains(QStringLiteral("toggle_gallery")));
  EXPECT_TRUE(widget.callbacks().contains(QStringLiteral("change_wallpaper")));
}

TEST_F(WallpapersTest, HotkeyOnTheMatchingScreenOpensTheGallery) {
  WallpapersWidget widget(context(options()), hooks(makeImages(2)));
  widget.setScreenName(QStringLiteral("DISPLAY1"));
  widget.show();

  emit eventBus_.widgetHotkey(QStringLiteral("wallpapers"), QStringLiteral("toggle_gallery"),
                              QStringLiteral("DISPLAY2"));
  EXPECT_EQ(widget.gallery(), nullptr);  // another bar's hotkey

  emit eventBus_.widgetHotkey(QStringLiteral("wallpapers"), QStringLiteral("toggle_gallery"),
                              QStringLiteral("DISPLAY1"));
  EXPECT_NE(widget.gallery(), nullptr);
}

TEST_F(WallpapersTest, ChangeWallpaperCallbackUsesTheManager) {
  const QStringList files = makeImages(2);
  WallpapersWidget widget(context(options()), hooks());
  rig_.picks = {1};
  EXPECT_TRUE(widget.runCallback(QStringLiteral("change_wallpaper")));
  ASSERT_EQ(rig_.desktop->sets.size(), 1u);
  EXPECT_EQ(rig_.desktop->sets[0].path, nativeAbsolute(files[1]));

  // An explicit image bypasses the random pick.
  EXPECT_TRUE(widget.runCallback(QStringLiteral("change_wallpaper %1").arg(files[0])));
  ASSERT_EQ(rig_.desktop->sets.size(), 2u);
  EXPECT_EQ(rig_.desktop->sets[1].path, nativeAbsolute(files[0]));
}

TEST_F(WallpapersTest, ConfiguresTheSharedManagerFromOptions) {
  json opts = options();
  opts["change_automatically"] = true;
  opts["update_interval"] = 120;
  opts["run_after"] = {"notify {image}"};
  WallpapersWidget widget(context(opts), hooks());
  EXPECT_TRUE(rig_.manager->timerRunning());
  EXPECT_EQ(rig_.manager->timerIntervalMs(), 120000);
  EXPECT_EQ(rig_.manager->imagePaths(), QStringList{qs(temp_.path().toStdString())});
}

TEST_F(WallpapersTest, TargetScreenUsesTheKeybindingMode) {
  json opts = options();
  opts["keybindings"] = json::array();
  WallpapersWidget defaultMode(context(opts), hooks());
  defaultMode.targetScreen();
  EXPECT_EQ(lastMode_, QStringLiteral("active"));

  opts["keybindings"] = {{{"keys", "alt+e"}, {"action", "toggle_gallery"}, {"screen", "primary"}}};
  WallpapersWidget primary(context(opts), hooks());
  primary.targetScreen();
  EXPECT_EQ(lastMode_, QStringLiteral("primary"));
}

TEST_F(WallpapersTest, WidgetTypeIsRegistered) {
  WidgetRegistry registry;
  registerWallpaperWidgets(registry);
  EXPECT_TRUE(registry.contains("yasb.wallpapers.WallpapersWidget"));
}

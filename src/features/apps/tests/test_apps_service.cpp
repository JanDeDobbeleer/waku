#include <gtest/gtest.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <vector>

#include "features/apps/qml/apps_service.h"

using namespace wabar::apps;

namespace {

QString s(const char* text) { return QString::fromLatin1(text); }

void touch(const QString& path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("x");
}

wabar::kernel::IconBitmap solid(int size, unsigned char b, unsigned char g, unsigned char r) {
  wabar::kernel::IconBitmap bitmap;
  bitmap.width = size;
  bitmap.height = size;
  for (int i = 0; i < size * size; ++i) {
    const char pixel[4] = {static_cast<char>(b), static_cast<char>(g), static_cast<char>(r),
                           static_cast<char>(255)};
    bitmap.bgra.append(pixel, 4);
  }
  return bitmap;
}

struct Launched {
  QString id;
  LaunchMode mode;
};

// A Start Menu in a temp folder, a fake AppsFolder, a launcher that starts nothing and an icon
// loader that paints.
struct Fixture {
  QTemporaryDir menu;
  QTemporaryDir data;
  std::vector<Launched> launched;
  bool launchResult = true;
  std::vector<AppRecord> storeApps;

  Fixture() {
    touch(menu.filePath(s("Alpha.lnk")));
    touch(menu.filePath(s("Dev Tools/Beta.lnk")));
    touch(menu.filePath(s("Visual Studio Code.lnk")));
    touch(menu.filePath(s("Uninstall Alpha.lnk")));
    AppRecord photos;
    photos.id = s("Microsoft.Photos_8wekyb3d8bbwe!App");
    photos.name = s("Photos");
    photos.kind = AppKind::Store;
    photos.aumid = photos.id;
    photos.iconSource = s("shell:AppsFolder\\") + photos.id;
    storeApps.push_back(photos);
  }

  AppsService::Deps deps(bool async = false) {
    AppsService::Deps deps;
    deps.folders = {menu.path()};
    deps.resolver = [](const QString& path) {
      ShortcutInfo info;
      info.ok = true;
      info.target = s("C:\\apps\\") + QFileInfo(path).completeBaseName() + s(".exe");
      info.description = s("About ") + QFileInfo(path).completeBaseName();
      info.workingDirectory = s("C:\\apps");
      return info;
    };
    deps.appsFolder = [this]() { return storeApps; };
    deps.launcher = [this](const AppRecord& record, LaunchMode mode) {
      launched.push_back({record.id, mode});
      if (record.kind == AppKind::Store && mode != LaunchMode::Normal) return false;
      return launchResult;
    };
    deps.iconLoader = [](const QString& source, int) {
      if (source.contains(s("Alpha"))) return solid(64, 0, 0, 255);
      if (source.contains(s("Photos"))) return solid(48, 0, 255, 0);
      return wabar::kernel::IconBitmap();
    };
    deps.frecencyPath = data.filePath(s("apps_frecency.json"));
    deps.async = async;
    return deps;
  }
};

int actionCount(DesktopEntry* entry) {
  auto actions = entry->actions();
  return static_cast<int>(actions.count(&actions));
}

QStringList idsOf(const QVariantList& list) {
  QStringList ids;
  for (const QVariant& item : list) ids.append(item.value<DesktopEntry*>()->id());
  return ids;
}

}  // namespace

TEST(AppsService, ScanFillsTheCatalogue) {
  Fixture fixture;
  AppsService service(fixture.deps());
  EXPECT_TRUE(service.ready());
  EXPECT_FALSE(service.loading());
  EXPECT_EQ(service.count(), 4);
  QStringList names;
  for (const DesktopEntry* entry : service.applications()->entries()) names.append(entry->name());
  EXPECT_EQ(names, (QStringList{s("Alpha"), s("Beta"), s("Photos"), s("Visual Studio Code")}));
  EXPECT_EQ(service.applications()->values().size(), 4);
  EXPECT_GE(service.scanMs(), 0);
}

TEST(AppsService, DesktopEntryMapsTheRecord) {
  Fixture fixture;
  AppsService service(fixture.deps());

  DesktopEntry* beta = service.byId(s("dev tools/beta"));  // case-insensitive
  ASSERT_TRUE(beta);
  EXPECT_EQ(beta->id(), s("Dev Tools/Beta"));
  EXPECT_EQ(beta->name(), s("Beta"));
  EXPECT_EQ(beta->comment(), s("About Beta"));
  EXPECT_EQ(beta->genericName(), QString());
  EXPECT_EQ(beta->execString(), s("\"C:\\apps\\Beta.exe\""));
  EXPECT_EQ(beta->command(), (QStringList{s("C:\\apps\\Beta.exe")}));
  EXPECT_EQ(beta->workingDirectory(), s("C:\\apps"));
  EXPECT_EQ(beta->startupClass(), s("Beta"));
  EXPECT_EQ(beta->categories(), (QStringList{s("Dev Tools")}));
  EXPECT_FALSE(beta->noDisplay());
  EXPECT_FALSE(beta->runInTerminal());
  EXPECT_TRUE(beta->isApplication());
  EXPECT_FALSE(beta->isStoreApp());
  EXPECT_EQ(beta->icon(), s("image://appicon/Dev%20Tools%2FBeta"));
  EXPECT_EQ(actionCount(beta), 2);

  DesktopEntry* photos = service.byId(s("Microsoft.Photos_8wekyb3d8bbwe!App"));
  ASSERT_TRUE(photos);
  EXPECT_TRUE(photos->isStoreApp());
  EXPECT_EQ(photos->aumid(), s("Microsoft.Photos_8wekyb3d8bbwe!App"));
  EXPECT_EQ(photos->execString(), s("shell:AppsFolder\\Microsoft.Photos_8wekyb3d8bbwe!App"));
  EXPECT_EQ(actionCount(photos), 0);

  EXPECT_EQ(service.byId(s("nothing")), nullptr);
}

TEST(AppsService, HeuristicLookupFindsWindowClasses) {
  Fixture fixture;
  AppsService service(fixture.deps());
  const auto id = [&](const char* name) {
    const DesktopEntry* entry = service.heuristicLookup(s(name));
    return entry ? entry->id() : QString();
  };
  EXPECT_EQ(id("alpha"), s("Alpha"));  // id or name
  EXPECT_EQ(id("Visual Studio Code"), s("Visual Studio Code"));
  EXPECT_EQ(id("Beta.exe"), s("Dev Tools/Beta"));  // process name, last id segment
  EXPECT_EQ(id("photos"), s("Microsoft.Photos_8wekyb3d8bbwe!App"));            // name
  EXPECT_EQ(id("microsoft.photos"), s("Microsoft.Photos_8wekyb3d8bbwe!App"));  // AUMID
  EXPECT_EQ(id("zzzz"), QString());
  EXPECT_EQ(id(""), QString());
}

TEST(AppsService, SearchRanksAndUsesFrecency) {
  Fixture fixture;
  // Two apps whose names match "vis" equally well; launching one decides the order.
  touch(fixture.menu.filePath(s("Visio.lnk")));
  touch(fixture.menu.filePath(s("Visor.lnk")));
  AppsService service(fixture.deps());

  EXPECT_EQ(idsOf(service.search(s("vis"), 0)),
            (QStringList{s("Visio"), s("Visor"), s("Visual Studio Code")}));
  EXPECT_EQ(idsOf(service.search(s("vis"), 1)), (QStringList{s("Visio")}));
  EXPECT_EQ(idsOf(service.search(s("studio"), 0)), (QStringList{s("Visual Studio Code")}));
  EXPECT_EQ(idsOf(service.search(s("alp"), 5)), (QStringList{s("Alpha")}));

  EXPECT_TRUE(service.launch(s("Visor")));
  EXPECT_EQ(idsOf(service.search(s("vis"), 0)),
            (QStringList{s("Visor"), s("Visio"), s("Visual Studio Code")}));
  // A blank query lists what is used first.
  EXPECT_EQ(idsOf(service.search(QString(), 1)), (QStringList{s("Visor")}));
}

TEST(AppsService, LaunchRecordsFrecencyAndPersistsIt) {
  Fixture fixture;
  {
    AppsService service(fixture.deps());
    EXPECT_TRUE(service.launch(s("Alpha")));
    EXPECT_TRUE(service.launchAsAdmin(s("Alpha")));
    EXPECT_TRUE(service.openLocation(s("Alpha")));
    EXPECT_FALSE(service.launchAsAdmin(s("Microsoft.Photos_8wekyb3d8bbwe!App")));
    EXPECT_FALSE(service.launch(s("unknown")));
    ASSERT_EQ(fixture.launched.size(), 4u);
    EXPECT_EQ(fixture.launched[0].mode, LaunchMode::Normal);
    EXPECT_EQ(fixture.launched[1].mode, LaunchMode::Admin);
    EXPECT_EQ(fixture.launched[2].mode, LaunchMode::OpenLocation);
    fixture.launchResult = false;
    EXPECT_FALSE(service.launch(s("Beta")));
    fixture.launchResult = true;
  }
  EXPECT_TRUE(QFileInfo::exists(fixture.data.filePath(s("apps_frecency.json"))));
  // A new service reads the file: Alpha (launched twice) leads a blank search; failed Beta does
  // not.
  AppsService again(fixture.deps());
  EXPECT_EQ(idsOf(again.search(QString(), 2)).first(), s("Alpha"));
  EXPECT_EQ(idsOf(again.search(QString(), 0)).last(), s("Visual Studio Code"));
}

TEST(AppsService, EntryExecuteAndActionsLaunch) {
  Fixture fixture;
  AppsService service(fixture.deps());
  DesktopEntry* beta = service.byId(s("Dev Tools/Beta"));
  ASSERT_TRUE(beta);
  beta->execute();
  auto actions = beta->actions();
  ASSERT_EQ(actions.count(&actions), 2);
  actions.at(&actions, 0)->execute();
  actions.at(&actions, 1)->execute();
  ASSERT_EQ(fixture.launched.size(), 3u);
  EXPECT_EQ(fixture.launched[0].mode, LaunchMode::Normal);
  EXPECT_EQ(fixture.launched[1].mode, LaunchMode::Admin);
  EXPECT_EQ(fixture.launched[2].mode, LaunchMode::OpenLocation);
  EXPECT_EQ(fixture.launched[2].id, s("Dev Tools/Beta"));
}

TEST(AppsService, RefreshKeepsEntryObjectsAndFollowsChanges) {
  Fixture fixture;
  AppsService service(fixture.deps());
  DesktopEntry* alpha = service.byId(s("Alpha"));
  ASSERT_TRUE(alpha);
  QSignalSpy changed(alpha, &DesktopEntry::changed);
  QSignalSpy counts(&service, &AppsService::countChanged);

  touch(fixture.menu.filePath(s("Delta.lnk")));
  QFile::remove(fixture.menu.filePath(s("Visual Studio Code.lnk")));
  service.refresh();
  EXPECT_EQ(service.count(), 4);
  EXPECT_EQ(service.byId(s("Alpha")), alpha);  // same object
  EXPECT_EQ(changed.count(), 0);               // nothing about it changed
  EXPECT_TRUE(service.byId(s("Delta")));
  EXPECT_EQ(service.byId(s("Visual Studio Code")), nullptr);
}

TEST(AppsService, AsyncScanPostsShortcutsThenTheStoreApps) {
  Fixture fixture;
  AppsService service(fixture.deps(true));
  EXPECT_TRUE(service.loading());
  QSignalSpy finished(&service, &AppsService::scanFinished);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() > 0; }, 10000));
  EXPECT_TRUE(service.ready());
  EXPECT_FALSE(service.loading());
  EXPECT_EQ(service.count(), 4);
  EXPECT_TRUE(service.byId(s("Microsoft.Photos_8wekyb3d8bbwe!App")));
}

TEST(AppsService, WatcherRescansAfterANewShortcut) {
  Fixture fixture;
  auto deps = fixture.deps(true);
  deps.watch = true;
  deps.debounceMs = 50;
  AppsService service(deps);
  QSignalSpy finished(&service, &AppsService::scanFinished);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() > 0; }, 10000));
  ASSERT_EQ(service.count(), 4);

  touch(fixture.menu.filePath(s("Dev Tools/Gamma.lnk")));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return service.byId(s("Dev Tools/Gamma")) != nullptr; }, 10000));
  EXPECT_EQ(service.count(), 5);
}

TEST(AppsService, DestroyingDuringAScanIsSafe) {
  Fixture fixture;
  {
    AppsService service(fixture.deps(true));
  }
  QTest::qWait(100);  // the worker finishes against a dead owner
  SUCCEED();
}

TEST(AppsService, TheRealConstructorStaysOffTheMachineWhenNotLive) {
  // The tests run on the offscreen platform: liveDesktop("apps") is false.
  AppsService service(nullptr);
  QSignalSpy finished(&service, &AppsService::scanFinished);
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.ready(); }, 10000));
  EXPECT_EQ(service.count(), 0);
  EXPECT_FALSE(service.launch(s("anything")));
}

TEST(AppsIconProvider, PaintsTheScaledIcon) {
  auto index = std::make_shared<IconIndex>();
  index->replace({{s("Alpha"), s("C:\\menu\\Alpha.lnk")}, {s("Dev Tools/Beta"), s("none")}});
  AppIconProvider provider(index, [](const QString& source, int size) {
    EXPECT_EQ(size, 32);
    return source.contains(s("Alpha")) ? solid(64, 0, 0, 255) : wabar::kernel::IconBitmap();
  });

  std::unique_ptr<QQuickImageResponse> response(
      provider.requestImageResponse(s("Alpha"), QSize(32, 32)));
  QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() > 0; }, 10000));
  std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
  ASSERT_TRUE(factory);
  const QImage image = factory->image();
  EXPECT_EQ(image.size(), QSize(32, 32));
  EXPECT_EQ(image.pixelColor(5, 5), QColor(255, 0, 0));

  // The percent-encoded id, as the entry's icon URL carries it, resolves too.
  std::unique_ptr<QQuickImageResponse> encoded(
      provider.requestImageResponse(s("Dev%20Tools%2FBeta"), QSize(32, 32)));
  QSignalSpy second(encoded.get(), &QQuickImageResponse::finished);
  ASSERT_TRUE(QTest::qWaitFor([&] { return second.count() > 0; }, 10000));
  EXPECT_FALSE(encoded->errorString().isEmpty());  // the loader had no icon for it

  std::unique_ptr<QQuickImageResponse> unknown(
      provider.requestImageResponse(s("missing"), QSize()));
  QSignalSpy third(unknown.get(), &QQuickImageResponse::finished);
  ASSERT_TRUE(QTest::qWaitFor([&] { return third.count() > 0; }, 10000));
  EXPECT_FALSE(unknown->errorString().isEmpty());
}

TEST(AppsIconProvider, IndexResolvesEncodedIds) {
  IconIndex index;
  index.replace({{s("Dev Tools/Beta"), s("x")}});
  EXPECT_EQ(index.sourceFor(s("Dev Tools/Beta")), s("x"));
  EXPECT_EQ(index.sourceFor(s("Dev%20Tools%2FBeta")), s("x"));
  EXPECT_EQ(index.sourceFor(s("nope")), QString());
  EXPECT_EQ(appIconUrl(s("A B/c!d")), s("image://appicon/A%20B%2Fc%21d"));
}

// `import Quickshell` shows the service as DesktopEntries, and registers image://appicon.
TEST(AppsQuickshell, DesktopEntriesLoadsFromQml) {
  Fixture fixture;
  AppsService::setFactoryForTests(
      [&fixture]() { return std::make_unique<AppsService>(fixture.deps()); });
  {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQml
import QtQml.Models
import Quickshell
QtObject {
    id: root
    property var values: DesktopEntries.applications.values
    property int count: DesktopEntries.applications.count
    property DesktopEntry typed: DesktopEntries.byId("Alpha")
    property string firstName: DesktopEntries.applications.values[0].name
    property string betaIcon: Quickshell.iconPath(DesktopEntries.byId("Dev Tools/Beta").icon)
    property var lookup: DesktopEntries.heuristicLookup("beta")
    property var none: DesktopEntries.byId("zzz")
    property var actions: typed.actions.length
    property var viaModel: Instantiator {
        model: DesktopEntries.applications
        delegate: QtObject {
            required property var modelData
            property string label: modelData.name
        }
    }
    function runAlpha() { typed.execute(); }
}
)",
                      QUrl());
    std::unique_ptr<QObject> root(component.create());
    ASSERT_TRUE(root) << component.errorString().toStdString();
    EXPECT_EQ(root->property("count").toInt(), 4);
    EXPECT_EQ(root->property("values").toList().size(), 4);
    EXPECT_EQ(root->property("firstName").toString(), s("Alpha"));
    EXPECT_EQ(root->property("betaIcon").toString(), s("image://appicon/Dev%20Tools%2FBeta"));
    EXPECT_EQ(root->property("typed").value<QObject*>()->property("name").toString(), s("Alpha"));
    EXPECT_EQ(root->property("lookup").value<QObject*>()->property("id").toString(),
              s("Dev Tools/Beta"));
    EXPECT_EQ(root->property("none").value<QObject*>(), nullptr);
    EXPECT_EQ(root->property("actions").toInt(), 2);
    auto* instantiator = root->property("viaModel").value<QObject*>();
    ASSERT_TRUE(instantiator);
    EXPECT_EQ(instantiator->property("count").toInt(), 4);

    QMetaObject::invokeMethod(root.get(), "runAlpha");
    ASSERT_EQ(fixture.launched.size(), 1u);
    EXPECT_EQ(fixture.launched[0].id, s("Alpha"));

    // create() registered the provider; the icon of Alpha comes from the fake loader.
    auto* provider = dynamic_cast<QQuickAsyncImageProvider*>(engine.imageProvider(s("appicon")));
    ASSERT_TRUE(provider);
    std::unique_ptr<QQuickImageResponse> response(
        provider->requestImageResponse(s("Alpha"), QSize(16, 16)));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() > 0; }, 10000));
    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
    EXPECT_EQ(factory->image().size(), QSize(16, 16));
  }
  AppsService::setFactoryForTests({});
}

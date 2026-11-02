#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QVariantMap>
#include <memory>

#include "features/brightness/testing/fake_brightness.h"
#include "features/control_center/qml/control_center_service.h"

using namespace waku::control_center;
using waku::brightness::FakeBrightness;

namespace {

const QUuid kBalanced(QStringLiteral("{381b4222-f694-41f0-9685-ff5bb260df2e}"));
const QUuid kSaver(QStringLiteral("{a1841308-3541-4fab-bc81-f71556f20b4a}"));
const QUuid kBetter(QStringLiteral("{00000000-0000-0000-0000-000000000000}"));
const QUuid kBest(QStringLiteral("{ded574b5-45a0-4f42-8737-46345c09c238}"));

struct World {
  QString dnd = QStringLiteral("disabled");
  int dndReads = 0;
  QString theme = QStringLiteral("light");
  int keyboardToggles = 0;
  QUuid activePlan = kBalanced;
  QString modeName = QStringLiteral("Balanced");
  bool modesSupported = true;
  unsigned long planResult = 0;
  QStringList launched;  // every Launcher call, by name or URI
};

ControlCenter::Backend backendFor(World* w) {
  ControlCenter::Backend b;
  b.dndStatus = [w] {
    ++w->dndReads;
    return w->dnd;
  };
  b.setDndStatus = [w](const QString& mode) {
    w->dnd = mode;
    return true;
  };
  b.themeMode = [w] { return w->theme; };
  b.toggleTheme = [w]() -> std::optional<QString> {
    w->theme = w->theme == QLatin1String("dark") ? QStringLiteral("light") : QStringLiteral("dark");
    return w->theme;
  };
  b.toggleTouchKeyboard = [w] { ++w->keyboardToggles; };
  b.powerPlans = [w](std::optional<QUuid>* active) {
    if (active) *active = w->activePlan;
    return std::vector<PowerPlan>{{kBalanced, QStringLiteral("Balanced")},
                                  {kSaver, QStringLiteral("Power saver")}};
  };
  b.setPowerPlan = [w](const QUuid& guid) {
    if (w->planResult == 0) w->activePlan = guid;
    return w->planResult;
  };
  b.powerModeSupported = [w] { return w->modesSupported; };
  b.powerModes = [] {
    return std::vector<PowerMode>{{QStringLiteral("Best power efficiency"), kSaver},
                                  {QStringLiteral("Balanced"), kBetter},
                                  {QStringLiteral("Best performance"), kBest}};
  };
  b.activePowerMode = [w]() -> std::optional<PowerMode> { return PowerMode{w->modeName, QUuid()}; };
  b.setPowerMode = [w](const QUuid& guid) {
    w->modeName = guid == kBest ? QStringLiteral("Best performance") : QStringLiteral("Balanced");
    return true;
  };
  b.userName = [] { return QStringLiteral("Jan"); };
  b.avatarPath = [] { return QStringLiteral("C:/pics/jan.png"); };
  return b;
}

ControlCenter::Launcher launcherFor(World* w) {
  ControlCenter::Launcher l;
  l.lock = [w] { w->launched << QStringLiteral("lock"); };
  l.sleep = [w] { w->launched << QStringLiteral("sleep"); };
  l.hibernate = [w] { w->launched << QStringLiteral("hibernate"); };
  l.signOut = [w] { w->launched << QStringLiteral("signOut"); };
  l.restart = [w] { w->launched << QStringLiteral("restart"); };
  l.shutdown = [w] { w->launched << QStringLiteral("shutdown"); };
  l.open = [w](const QString& uri) { w->launched << uri; };
  return l;
}

struct Fixture {
  World world;
  FakeBrightness* brightness = new FakeBrightness;
  std::unique_ptr<ControlCenter> service =
      std::make_unique<ControlCenter>(backendFor(&world), launcherFor(&world), brightness, nullptr);
};

}  // namespace

TEST(ControlCenterService, ReadsTheInitialState) {
  Fixture f;
  EXPECT_EQ(f.service->dnd(), QStringLiteral("disabled"));
  EXPECT_FALSE(f.service->dndActive());
  EXPECT_FALSE(f.service->darkMode());
  EXPECT_EQ(f.service->userName(), QStringLiteral("Jan"));
  EXPECT_EQ(f.service->avatarUrl(), QStringLiteral("file:///C:/pics/jan.png"));
  EXPECT_EQ(f.service->powerPlanName(), QStringLiteral("Balanced"));
  EXPECT_EQ(f.service->powerModeName(), QStringLiteral("Balanced"));
}

TEST(ControlCenterService, ToggleDndFlipsBetweenDisabledAndPriority) {
  Fixture f;
  QSignalSpy changed(f.service.get(), &ControlCenter::changed);
  f.service->toggleDnd();
  EXPECT_EQ(f.world.dnd, QStringLiteral("priority"));
  EXPECT_TRUE(f.service->dndActive());
  f.service->toggleDnd();
  EXPECT_EQ(f.world.dnd, QStringLiteral("disabled"));
  EXPECT_FALSE(f.service->dndActive());
  EXPECT_EQ(changed.count(), 2);
}

TEST(ControlCenterService, CycleDndWalksAllThreeModes) {
  Fixture f;
  f.service->cycleDnd();
  EXPECT_EQ(f.service->dnd(), QStringLiteral("priority"));
  f.service->cycleDnd();
  EXPECT_EQ(f.service->dnd(), QStringLiteral("alarms"));
  f.service->cycleDnd();
  EXPECT_EQ(f.service->dnd(), QStringLiteral("disabled"));
}

TEST(ControlCenterService, ExternalDndChangeIsMirrored) {
  Fixture f;
  QSignalSpy changed(f.service.get(), &ControlCenter::changed);
  f.service->onDndStatus(QStringLiteral("alarms"));
  EXPECT_TRUE(f.service->dndActive());
  f.service->onDndStatus(QStringLiteral("alarms"));
  EXPECT_EQ(changed.count(), 1);
  f.service->onDndStatus(QStringLiteral("unknown"));
  EXPECT_TRUE(f.service->dndActive());  // as Python: only "disabled" is off
}

TEST(ControlCenterService, ToggleThemeAndKeyboard) {
  Fixture f;
  f.service->toggleTheme();
  EXPECT_TRUE(f.service->darkMode());
  f.service->toggleTheme();
  EXPECT_FALSE(f.service->darkMode());
  f.service->toggleTouchKeyboard();
  EXPECT_EQ(f.world.keyboardToggles, 1);
}

TEST(ControlCenterService, ScreenshotAndSettingsGoThroughTheLauncher) {
  Fixture f;
  f.service->screenshot();
  f.service->openSettings(QStringLiteral("ms-settings:network"));
  f.service->openSettings(QStringLiteral("calc.exe"));  // not a Settings page: ignored
  EXPECT_EQ(f.world.launched,
            (QStringList{QStringLiteral("ms-screenclip:"), QStringLiteral("ms-settings:network")}));
}

TEST(ControlCenterService, BrightnessTargetsTheFirstMonitorThenTheChosenOne) {
  Fixture f;
  EXPECT_TRUE(f.service->brightnessAvailable());
  EXPECT_EQ(f.service->brightness(), 40);
  EXPECT_EQ(f.service->brightnessSource(), QStringLiteral("7"));
  ASSERT_EQ(f.service->brightnessSources().size(), 2);
  EXPECT_EQ(f.service->brightnessSources().at(1).toMap().value(QStringLiteral("id")).toString(),
            QStringLiteral("9"));

  f.service->setBrightness(55);
  ASSERT_EQ(f.brightness->writes.size(), 1u);
  EXPECT_EQ(f.brightness->writes[0], (std::pair<qulonglong, int>(7, 55)));

  f.service->setBrightnessSource(QStringLiteral("9"));
  EXPECT_EQ(f.service->brightness(), 80);
  f.service->setBrightness(150);
  EXPECT_EQ(f.brightness->writes.back(), (std::pair<qulonglong, int>(9, 100)));
  f.service->setBrightnessSource(QStringLiteral("not a number"));
  EXPECT_EQ(f.service->brightnessSource(), QStringLiteral("9"));
}

TEST(ControlCenterService, BrightnessFollowsExternalChanges) {
  Fixture f;
  f.brightness->values[7] = 12;
  QSignalSpy changed(f.service.get(), &ControlCenter::changed);
  emit f.brightness->brightnessChanged(7, 12);
  EXPECT_EQ(f.service->brightness(), 12);
  EXPECT_EQ(changed.count(), 1);
}

TEST(ControlCenterService, NoBrightnessControlMeansUnavailable) {
  World world;
  ControlCenter service(backendFor(&world), launcherFor(&world), nullptr, nullptr);
  EXPECT_FALSE(service.brightnessAvailable());
  EXPECT_TRUE(service.brightnessSources().isEmpty());
  service.setBrightness(30);  // nothing to write, no crash
}

TEST(ControlCenterService, RefreshRereadsAndAsksForADdcRead) {
  Fixture f;
  f.world.dnd = QStringLiteral("priority");
  f.world.theme = QStringLiteral("dark");
  f.service->refresh();
  EXPECT_TRUE(f.service->dndActive());
  EXPECT_TRUE(f.service->darkMode());
  EXPECT_EQ(f.brightness->refreshes, 1);
}

TEST(ControlCenterService, ListsAndSwitchesPowerPlans) {
  Fixture f;
  const QVariantList plans = f.service->powerPlans();
  ASSERT_EQ(plans.size(), 2);
  EXPECT_TRUE(plans.at(0).toMap().value(QStringLiteral("active")).toBool());
  EXPECT_FALSE(plans.at(1).toMap().value(QStringLiteral("active")).toBool());

  const QString saver = plans.at(1).toMap().value(QStringLiteral("id")).toString();
  EXPECT_TRUE(f.service->setPowerPlan(saver));
  EXPECT_EQ(f.world.activePlan, kSaver);
  EXPECT_EQ(f.service->powerPlanName(), QStringLiteral("Power saver"));
  EXPECT_FALSE(f.service->setPowerPlan(QStringLiteral("{11111111-1111-1111-1111-111111111111}")));
}

TEST(ControlCenterService, FailedPlanSwitchKeepsTheActivePlan) {
  Fixture f;
  f.world.planResult = 5;
  const QString saver =
      f.service->powerPlans().at(1).toMap().value(QStringLiteral("id")).toString();
  EXPECT_FALSE(f.service->setPowerPlan(saver));
  EXPECT_EQ(f.service->powerPlanName(), QStringLiteral("Balanced"));
}

TEST(ControlCenterService, ListsAndSwitchesPowerModes) {
  Fixture f;
  EXPECT_TRUE(f.service->powerModeAvailable());
  const QVariantList modes = f.service->powerModes();
  ASSERT_EQ(modes.size(), 3);
  const QString best = modes.at(2).toMap().value(QStringLiteral("id")).toString();
  EXPECT_TRUE(f.service->setPowerMode(best));
  EXPECT_EQ(f.service->powerModeName(), QStringLiteral("Best performance"));
}

TEST(ControlCenterService, UnsupportedPowerModesAreHidden) {
  World world;
  world.modesSupported = false;
  ControlCenter service(backendFor(&world), launcherFor(&world), nullptr, nullptr);
  EXPECT_FALSE(service.powerModeAvailable());
  EXPECT_TRUE(service.powerModes().isEmpty());
  EXPECT_FALSE(service.setPowerMode(kBest.toString(QUuid::WithoutBraces)));
}

TEST(ControlCenterService, SafeActionsRunAtOnce) {
  Fixture f;
  EXPECT_TRUE(f.service->requestAction(QStringLiteral("lock")));
  EXPECT_TRUE(f.service->requestAction(QStringLiteral("sleep")));
  EXPECT_TRUE(f.service->requestAction(QStringLiteral("hibernate")));
  EXPECT_EQ(f.world.launched, (QStringList{QStringLiteral("lock"), QStringLiteral("sleep"),
                                           QStringLiteral("hibernate")}));
}

TEST(ControlCenterService, DestructiveActionsNeedASecondRequest) {
  Fixture f;
  for (const char* id : {"signOut", "restart", "shutdown"}) {
    const QString action = QString::fromLatin1(id);
    EXPECT_FALSE(f.service->requestAction(action));
    EXPECT_EQ(f.service->pendingAction(), action);
    EXPECT_FALSE(f.world.launched.contains(action));
    EXPECT_TRUE(f.service->requestAction(action));
    EXPECT_TRUE(f.service->pendingAction().isEmpty());
  }
  EXPECT_EQ(f.world.launched, (QStringList{QStringLiteral("signOut"), QStringLiteral("restart"),
                                           QStringLiteral("shutdown")}));
}

TEST(ControlCenterService, ADifferentActionRestartsTheConfirmation) {
  Fixture f;
  EXPECT_FALSE(f.service->requestAction(QStringLiteral("shutdown")));
  EXPECT_FALSE(f.service->requestAction(QStringLiteral("restart")));
  EXPECT_EQ(f.service->pendingAction(), QStringLiteral("restart"));
  EXPECT_TRUE(f.world.launched.isEmpty());
}

TEST(ControlCenterService, CancelClearsThePendingAction) {
  Fixture f;
  EXPECT_FALSE(f.service->requestAction(QStringLiteral("shutdown")));
  f.service->cancelAction();
  EXPECT_TRUE(f.service->pendingAction().isEmpty());
  EXPECT_FALSE(f.service->requestAction(QStringLiteral("shutdown")));  // asks again
  EXPECT_TRUE(f.world.launched.isEmpty());
}

TEST(ControlCenterService, UnknownActionDoesNothing) {
  Fixture f;
  EXPECT_FALSE(f.service->requestAction(QStringLiteral("format")));
  EXPECT_TRUE(f.service->pendingAction().isEmpty());
  EXPECT_TRUE(f.world.launched.isEmpty());
}

TEST(ControlCenterService, ServiceWithoutALauncherCannotRunActions) {
  World world;
  ControlCenter service(backendFor(&world), ControlCenter::Launcher{}, nullptr, nullptr);
  EXPECT_FALSE(service.requestAction(QStringLiteral("lock")));
  EXPECT_FALSE(service.requestAction(QStringLiteral("shutdown")));
  EXPECT_TRUE(service.pendingAction().isEmpty());
}

TEST(ControlCenterService, StopsTheBrightnessControlOnDestruction) {
  World world;
  int stops = 0;
  {
    auto* brightness = new FakeBrightness;
    brightness->stopCounter = &stops;
    ControlCenter service(backendFor(&world), launcherFor(&world), brightness, nullptr);
    EXPECT_EQ(stops, 0);
  }
  EXPECT_EQ(stops, 1);
}

TEST(ControlCenterService, RealServiceIsInertOffscreen) {
  // The suite runs on the offscreen platform: no registry, power, DND or session call is made.
  ControlCenter service;
  EXPECT_FALSE(service.available());
  EXPECT_FALSE(service.brightnessAvailable());
  EXPECT_TRUE(service.powerPlans().isEmpty());
  EXPECT_FALSE(service.requestAction(QStringLiteral("lock")));
  EXPECT_FALSE(service.requestAction(QStringLiteral("shutdown")));
  service.toggleDnd();
  service.toggleTheme();
  service.screenshot();
}

TEST(ControlCenterService, EachGroupAnnouncesOnlyItself) {
  Fixture f;
  QSignalSpy dnd(f.service.get(), &ControlCenter::dndChanged);
  QSignalSpy brightness(f.service.get(), &ControlCenter::brightnessChanged);
  QSignalSpy sources(f.service.get(), &ControlCenter::brightnessSourcesChanged);
  QSignalSpy power(f.service.get(), &ControlCenter::powerChanged);
  QSignalSpy changed(f.service.get(), &ControlCenter::changed);
  f.service->setBrightness(40);  // a slider tick: the plan, mode and monitor lists stay
  EXPECT_EQ(brightness.count(), 1);
  EXPECT_EQ(power.count(), 0);
  EXPECT_EQ(sources.count(), 0);
  EXPECT_EQ(dnd.count(), 0);
  EXPECT_EQ(changed.count(), 1);
  f.service->toggleDnd();
  EXPECT_EQ(dnd.count(), 1);
  EXPECT_EQ(power.count(), 0);
  f.service->refresh();  // everything
  EXPECT_EQ(power.count(), 1);
  EXPECT_EQ(dnd.count(), 2);
  // Each property notifies with its own group.
  const QMetaObject* meta = f.service->metaObject();
  const auto notifyOf = [&](const char* name) {
    return QString::fromLatin1(meta->property(meta->indexOfProperty(name)).notifySignal().name());
  };
  EXPECT_EQ(notifyOf("powerPlans"), QStringLiteral("powerChanged"));
  EXPECT_EQ(notifyOf("brightnessSources"), QStringLiteral("brightnessSourcesChanged"));
  EXPECT_EQ(notifyOf("brightness"), QStringLiteral("brightnessChanged"));
  EXPECT_EQ(notifyOf("userName"), QStringLiteral("userChanged"));
}

TEST(ControlCenterService, ToggleDndUsesTheMirroredState) {
  Fixture f;
  const int reads = f.world.dndReads;
  f.service->toggleDnd();
  f.service->cycleDnd();
  EXPECT_EQ(f.world.dndReads, reads);  // before: one out-of-process read per click
  EXPECT_EQ(f.service->dnd(), QStringLiteral("alarms"));
}

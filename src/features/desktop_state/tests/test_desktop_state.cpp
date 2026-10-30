#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#include "features/desktop_state/qml/desktop_state.h"
#include "features/desktop_state/qml/idle_inhibitor.h"
#include "features/desktop_state/testing/fake_desktop_probe.h"

using namespace wabar::desktop_state;

namespace {

struct Harness {
  Harness() {
    auto fake = std::make_unique<FakeProbe>();
    probe = fake.get();
    state = std::make_unique<DesktopState>(std::move(fake), nullptr);
  }
  FakeProbe* probe = nullptr;
  std::unique_ptr<DesktopState> state;
};

}  // namespace

TEST(DesktopState, ReadsTheProbeOnDemandWithoutPolling) {
  Harness h;
  EXPECT_TRUE(h.state->available());
  EXPECT_FALSE(h.state->polling());
  h.probe->keys.caps = true;
  h.probe->keys.scroll = true;
  h.probe->idle = 12.9;
  EXPECT_TRUE(h.state->capsLock());
  EXPECT_FALSE(h.state->numLock());
  EXPECT_TRUE(h.state->scrollLock());
  EXPECT_EQ(h.state->idleSeconds(), 12);
  EXPECT_EQ(h.state->keyboardLayout(), QStringLiteral("EN"));
  EXPECT_EQ(h.state->keyboardLayoutName(), QStringLiteral("English (United States)"));
  EXPECT_EQ(h.state->keyboardLayoutId(), QStringLiteral("04090409"));
  EXPECT_FALSE(h.state->polling());
}

TEST(DesktopState, WithoutAProbeStaysAtRest) {
  DesktopState none(nullptr, nullptr);
  EXPECT_FALSE(none.available());
  EXPECT_FALSE(none.capsLock());
  EXPECT_TRUE(none.keyboardLayout().isEmpty());
  EXPECT_EQ(none.idleSeconds(), 0);
  none.refresh();
  EXPECT_FALSE(none.polling());
}

TEST(DesktopState, PollsOnlyWhileSomethingObserves) {
  Harness h;
  EXPECT_FALSE(h.state->polling());
  auto connection = std::make_unique<QMetaObject::Connection>(
      QObject::connect(h.state.get(), &DesktopState::capsLockChanged, h.state.get(), [] {}));
  QCoreApplication::processEvents();
  EXPECT_TRUE(h.state->polling());
  QObject::disconnect(*connection);
  QCoreApplication::processEvents();
  EXPECT_FALSE(h.state->polling());
}

TEST(DesktopState, EmitsChangeSignalsOncePerChange) {
  Harness h;
  QSignalSpy caps(h.state.get(), &DesktopState::capsLockChanged);
  QSignalSpy num(h.state.get(), &DesktopState::numLockChanged);
  QSignalSpy layout(h.state.get(), &DesktopState::keyboardLayoutChanged);
  QSignalSpy idle(h.state.get(), &DesktopState::idleSecondsChanged);
  QCoreApplication::processEvents();
  ASSERT_TRUE(h.state->polling());
  h.probe->keys.caps = true;
  h.probe->layout.shortName = QStringLiteral("FR");
  h.probe->idle = 3.2;
  QTest::qWait(DesktopState::kPollIntervalMs * 2);
  EXPECT_EQ(caps.count(), 1);
  EXPECT_EQ(num.count(), 0);
  EXPECT_EQ(layout.count(), 1);
  EXPECT_EQ(idle.count(), 1);
  // Same whole second: no signal.
  h.probe->idle = 3.9;
  QTest::qWait(DesktopState::kPollIntervalMs * 2);
  EXPECT_EQ(idle.count(), 1);
  EXPECT_TRUE(h.state->capsLock());
}

TEST(DesktopState, UnreadableIdleTimeCountsAsZero) {
  Harness h;
  h.probe->idle = 5.0;
  EXPECT_EQ(h.state->idleSeconds(), 5);
  h.probe->idle = std::nullopt;
  EXPECT_EQ(h.state->idleSeconds(), 0);
}

TEST(DesktopState, IsUsableFromQmlAndPollsForABinding) {
  Harness h;
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("desktop"), h.state.get());
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
QtObject {
    property bool caps: desktop.capsLock
    property string layout: desktop.keyboardLayout
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  QCoreApplication::processEvents();
  EXPECT_TRUE(h.state->polling());
  EXPECT_FALSE(root->property("caps").toBool());
  h.probe->keys.caps = true;
  QTest::qWait(DesktopState::kPollIntervalMs * 2);
  EXPECT_TRUE(root->property("caps").toBool());
  EXPECT_EQ(root->property("layout").toString(), QStringLiteral("EN"));
}

TEST(IdleInhibitor, HoldsTheExecutionStateWhileEnabled) {
  FakeProbe probe;
  IdleInhibitCounter counter(&probe);
  IdleInhibitor inhibitor(&counter, nullptr);
  EXPECT_FALSE(probe.awake);
  inhibitor.setEnabled(true);
  EXPECT_TRUE(probe.awake);
  inhibitor.setEnabled(false);
  EXPECT_FALSE(probe.awake);
  EXPECT_EQ(probe.awakeCalls, 2);
}

TEST(IdleInhibitor, ReleasesOnDestruction) {
  FakeProbe probe;
  IdleInhibitCounter counter(&probe);
  {
    IdleInhibitor inhibitor(&counter, nullptr);
    inhibitor.setEnabled(true);
    EXPECT_TRUE(probe.awake);
  }
  EXPECT_FALSE(probe.awake);
  EXPECT_EQ(counter.count(), 0);
}

TEST(IdleInhibitor, SeveralInhibitorsShareOneState) {
  FakeProbe probe;
  IdleInhibitCounter counter(&probe);
  IdleInhibitor a(&counter, nullptr);
  IdleInhibitor b(&counter, nullptr);
  a.setEnabled(true);
  b.setEnabled(true);
  a.setEnabled(true);  // repeated: no double count
  EXPECT_EQ(counter.count(), 2);
  EXPECT_EQ(probe.awakeCalls, 1);
  a.setEnabled(false);
  EXPECT_TRUE(probe.awake);
  b.setEnabled(false);
  EXPECT_FALSE(probe.awake);
  EXPECT_EQ(probe.awakeCalls, 2);
}

TEST(IdleInhibitor, CounterWithoutAProbeIsInert) {
  IdleInhibitCounter counter(nullptr);
  IdleInhibitor inhibitor(&counter, nullptr);
  inhibitor.setEnabled(true);
  EXPECT_EQ(counter.count(), 1);
  inhibitor.setEnabled(false);
  EXPECT_EQ(counter.count(), 0);
}

TEST(IdleInhibitor, IsUsableFromQmlWithTheWindowProperty) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import wabar.services
QtObject {
    property QtObject inhibitor: IdleInhibitor {
        enabled: false
        window: QtObject {}
    }
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  auto* inhibitor = root->property("inhibitor").value<QObject*>();
  ASSERT_NE(inhibitor, nullptr);
  EXPECT_FALSE(inhibitor->property("enabled").toBool());
  EXPECT_NE(inhibitor->property("window").value<QObject*>(), nullptr);
}

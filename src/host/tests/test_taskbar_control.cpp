// Hiding the Windows taskbar: the logic over fakes. Nothing here touches the real taskbar.
#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "host/qml/control_handler.h"
#include "host/qml/taskbar_command.h"
#include "host/taskbar_control.h"
#include "host/testing/shell_fixtures.h"
#include "host/testing/taskbar_fakes.h"

using namespace wabar::host;

namespace {

constexpr qint64 kOwnPid = 100;
constexpr qint64 kOtherPid = 200;

struct Fixture {
  FakeTaskbarSystem system;
  FakeMarkerStore store;
  WabarTaskbar taskbar{system, store, kOwnPid};
};

}  // namespace

TEST(TaskbarControlTest, HideTurnsAutoHideOnBeforeHidingTheWindowsAndRestoreReversesIt) {
  Fixture f;
  f.taskbar.hide();
  EXPECT_EQ(f.system.calls, (QStringList{"autohide-on", "hide-windows"}));
  EXPECT_TRUE(f.store.marker.has_value());
  EXPECT_EQ(f.taskbar.state(), TaskbarState::Hidden);

  f.system.calls.clear();
  f.taskbar.restore();
  EXPECT_EQ(f.system.calls, (QStringList{"show-windows", "autohide-off"}));
  EXPECT_FALSE(f.store.marker.has_value());
  EXPECT_EQ(f.taskbar.state(), TaskbarState::Visible);
}

TEST(TaskbarControlTest, ARestoreKeepsAutoHideThatWasAlreadyOn) {
  Fixture f;
  f.system.autoHide_ = true;
  f.taskbar.hide();
  EXPECT_EQ(f.system.calls, (QStringList{"hide-windows"}));
  ASSERT_TRUE(f.store.marker.has_value());
  EXPECT_FALSE(f.store.marker->changedAutoHide);

  f.system.calls.clear();
  f.taskbar.restore();
  EXPECT_EQ(f.system.calls, (QStringList{"show-windows"}));
  EXPECT_TRUE(f.system.autoHide_);
  EXPECT_EQ(f.taskbar.state(), TaskbarState::AutoHide);
}

TEST(TaskbarControlTest, ASecondHideChangesNothing) {
  Fixture f;
  f.taskbar.hide();
  f.system.calls.clear();
  f.taskbar.hide();
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_EQ(f.store.writes, 1);
}

TEST(TaskbarControlTest, ARestoreWithoutAHideChangesNothing) {
  Fixture f;
  f.taskbar.restore();
  EXPECT_TRUE(f.system.calls.isEmpty());
}

TEST(TaskbarControlTest, DoesNotFightAnotherToolThatAlreadyHidesTheWindows) {
  Fixture f;
  f.system.windowsVisible_ = false;  // thide
  f.system.autoHide_ = true;
  f.taskbar.hide();
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_FALSE(f.store.marker.has_value());            // wabar changed nothing, owns nothing
  EXPECT_EQ(f.taskbar.state(), TaskbarState::Hidden);  // reports what is seen
  f.taskbar.restore();
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_FALSE(f.system.windowsVisible_);  // still hidden: it is the other tool's doing
}

TEST(TaskbarControlTest, RestoresOnDestruction) {
  FakeTaskbarSystem system;
  FakeMarkerStore store;
  {
    WabarTaskbar taskbar(system, store, kOwnPid);
    taskbar.hide();
  }
  EXPECT_TRUE(system.windowsVisible_);
  EXPECT_FALSE(system.autoHide_);
  EXPECT_FALSE(store.marker.has_value());
}

TEST(TaskbarControlTest, ExplorerRestartHidesTheNewWindowsAgain) {
  Fixture f;
  f.taskbar.hide();
  f.system.restartExplorer();
  EXPECT_FALSE(f.system.windowsVisible_);
  // After a restore the restart is ignored.
  f.taskbar.restore();
  f.system.restartExplorer();
  EXPECT_TRUE(f.system.windowsVisible_);
}

TEST(TaskbarControlTest, AStaleMarkerWithADeadPidIsUndoneBeforeAnythingElse) {
  Fixture f;
  f.system.autoHide_ = true;
  f.system.windowsVisible_ = false;  // what the crashed run left
  f.store.marker = TaskbarMarker{kOtherPid, true, true};
  EXPECT_TRUE(f.taskbar.recoverStale());
  EXPECT_EQ(f.system.calls, (QStringList{"show-windows", "autohide-off"}));
  EXPECT_FALSE(f.store.marker.has_value());
}

TEST(TaskbarControlTest, AMarkerOfALiveWabarIsLeftAlone) {
  Fixture f;
  f.system.alive.insert(kOtherPid);
  f.system.windowsVisible_ = false;
  f.store.marker = TaskbarMarker{kOtherPid, true, true};
  EXPECT_FALSE(f.taskbar.recoverStale());
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_TRUE(f.store.marker.has_value());
}

TEST(TaskbarControlTest, NoMarkerMeansNoCalls) {
  Fixture f;
  EXPECT_FALSE(f.taskbar.recoverStale());
  f.taskbar.start(false);
  EXPECT_TRUE(f.system.calls.isEmpty());
}

TEST(TaskbarControlTest, StartRecoversFirstThenHidesOnlyWhenAsked) {
  Fixture f;
  f.system.autoHide_ = true;
  f.system.windowsVisible_ = false;
  f.store.marker = TaskbarMarker{kOtherPid, true, true};
  f.taskbar.start(true);
  EXPECT_EQ(f.system.calls,
            (QStringList{"show-windows", "autohide-off", "autohide-on", "hide-windows"}));
}

TEST(TaskbarControlTest, WithoutTheFlagNothingIsHidden) {
  Fixture f;
  f.taskbar.start(false);
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_EQ(f.taskbar.state(), TaskbarState::Visible);
}

TEST(TaskbarControlTest, TheMarkerTextRoundTripsAndRejectsOtherText) {
  const TaskbarMarker marker{4242, true, false};
  const QString text = formatTaskbarMarker(marker);
  EXPECT_TRUE(text.startsWith(QStringLiteral("taskbar hidden by wabar")));
  EXPECT_TRUE(text.contains(QStringLiteral("previous_autohide=0")));
  EXPECT_EQ(parseTaskbarMarker(text), marker);
  EXPECT_FALSE(parseTaskbarMarker(QStringLiteral("hello")).has_value());
  EXPECT_FALSE(parseTaskbarMarker(QStringLiteral("taskbar hidden by wabar\n")).has_value());
}

TEST(TaskbarControlTest, TheFileMarkerStoreWritesReadsAndDeletes) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  auto store = makeFileMarkerStore(dir.filePath(QStringLiteral("sub/taskbar_hidden.marker")));
  EXPECT_FALSE(store->read().has_value());
  store->write({77, true, true});
  EXPECT_EQ(store->read(), (TaskbarMarker{77, true, true}));
  store->remove();
  EXPECT_FALSE(store->read().has_value());
}

// ---- the `taskbar` verbs ----

TEST(TaskbarCommandTest, HideShowAndStatusAnswerTheState) {
  FakeTaskbarControl taskbar;
  EXPECT_EQ(answerTaskbarCommand({"status"}, &taskbar), QStringLiteral("ok: taskbar: visible"));
  EXPECT_EQ(answerTaskbarCommand({"hide"}, &taskbar), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(taskbar.hides, 1);
  EXPECT_EQ(answerTaskbarCommand({"status"}, &taskbar), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(answerTaskbarCommand({"show"}, &taskbar), QStringLiteral("ok: taskbar: visible"));
  EXPECT_EQ(taskbar.restores, 1);
}

TEST(TaskbarCommandTest, StatusNeverChangesTheTaskbar) {
  FakeTaskbarControl taskbar;
  answerTaskbarCommand({"status"}, &taskbar);
  EXPECT_EQ(taskbar.hides + taskbar.restores, 0);
  taskbar.state_ = TaskbarState::AutoHide;
  EXPECT_EQ(answerTaskbarCommand({"status"}, &taskbar),
            QStringLiteral("ok: taskbar: autohide-was-on"));
}

TEST(TaskbarCommandTest, BadInputIsAnError) {
  FakeTaskbarControl taskbar;
  EXPECT_TRUE(answerTaskbarCommand({}, &taskbar).startsWith(QStringLiteral("error: usage")));
  EXPECT_TRUE(
      answerTaskbarCommand({"hide", "x"}, &taskbar).startsWith(QStringLiteral("error: usage")));
  EXPECT_TRUE(answerTaskbarCommand({"explode"}, &taskbar)
                  .startsWith(QStringLiteral("error: unknown taskbar command 'explode'")));
  EXPECT_EQ(answerTaskbarCommand({"hide"}, nullptr),
            QStringLiteral("error: the taskbar is not available"));
  EXPECT_EQ(taskbar.hides, 0);
}

TEST(TaskbarCommandTest, TheControlHandlerRoutesTheVerbToTheTaskbar) {
  Harness h;
  h.start(shellConfig());
  FakeTaskbarControl taskbar;
  const wabar::control::ControlHandler handler =
      makeControlHandler(*h.host, false, {}, {}, &taskbar);
  EXPECT_EQ(handler(QStringLiteral("taskbar hide")), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(handler(QStringLiteral("taskbar status")), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(handler(QStringLiteral("taskbar show")), QStringLiteral("ok: taskbar: visible"));
  EXPECT_EQ(taskbar.hides, 1);
  EXPECT_EQ(taskbar.restores, 1);
  // Without a taskbar (offscreen platform) the verb is an error, not a crash.
  const wabar::control::ControlHandler bare = makeControlHandler(*h.host, false);
  EXPECT_EQ(bare(QStringLiteral("taskbar hide")),
            QStringLiteral("error: the taskbar is not available"));
}

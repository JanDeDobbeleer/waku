// Hiding the Windows taskbar: the logic over fakes. Nothing here touches the real taskbar.
#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "host/qml/control_handler.h"
#include "host/qml/windows_taskbar_command.h"
#include "host/testing/shell_fixtures.h"
#include "host/testing/windows_taskbar_fakes.h"
#include "host/windows_taskbar.h"

using namespace waku::host;

namespace {

constexpr qint64 kOwnPid = 100;
constexpr qint64 kOtherPid = 200;

struct Fixture {
  FakeTaskbarSystem system;
  FakeMarkerStore store;
  WindowsTaskbarHider taskbar{system, store, kOwnPid};
};

}  // namespace

TEST(WindowsTaskbarTest, HideTurnsAutoHideOnBeforeHidingTheWindowsAndRestoreReversesIt) {
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

TEST(WindowsTaskbarTest, ARestoreKeepsAutoHideThatWasAlreadyOn) {
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

TEST(WindowsTaskbarTest, ASecondHideChangesNothing) {
  Fixture f;
  f.taskbar.hide();
  f.system.calls.clear();
  f.taskbar.hide();
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_EQ(f.store.writes, 1);
}

TEST(WindowsTaskbarTest, ARestoreWithoutAHideChangesNothing) {
  Fixture f;
  f.taskbar.restore();
  EXPECT_TRUE(f.system.calls.isEmpty());
}

TEST(WindowsTaskbarTest, DoesNotFightAnotherToolThatAlreadyHidesTheWindows) {
  Fixture f;
  f.system.windowsVisible_ = false;  // thide
  f.system.autoHide_ = true;
  f.taskbar.hide();
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_FALSE(f.store.marker.has_value());            // waku changed nothing, owns nothing
  EXPECT_EQ(f.taskbar.state(), TaskbarState::Hidden);  // reports what is seen
  f.taskbar.restore();
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_FALSE(f.system.windowsVisible_);  // still hidden: it is the other tool's doing
}

TEST(WindowsTaskbarTest, RestoresOnDestruction) {
  FakeTaskbarSystem system;
  FakeMarkerStore store;
  {
    WindowsTaskbarHider taskbar(system, store, kOwnPid);
    taskbar.hide();
  }
  EXPECT_TRUE(system.windowsVisible_);
  EXPECT_FALSE(system.autoHide_);
  EXPECT_FALSE(store.marker.has_value());
}

TEST(WindowsTaskbarTest, TheRecheckHidesATaskbarThatCameBack) {
  Fixture f;
  f.taskbar.hide();
  EXPECT_FALSE(f.system.windowsVisible_);
  // Explorer shows an auto-hide taskbar again when the work area changes; the once-a-second
  // recheck is the same callback as the restart one.
  f.system.windowsVisible_ = true;
  f.system.restartCallback();
  EXPECT_FALSE(f.system.windowsVisible_);
  f.taskbar.restore();
  EXPECT_TRUE(f.system.windowsVisible_);
}

TEST(WindowsTaskbarTest, ExplorerRestartHidesTheNewWindowsAgain) {
  Fixture f;
  f.taskbar.hide();
  f.system.restartExplorer();
  EXPECT_FALSE(f.system.windowsVisible_);
  // After a restore the restart is ignored.
  f.taskbar.restore();
  f.system.restartExplorer();
  EXPECT_TRUE(f.system.windowsVisible_);
}

TEST(WindowsTaskbarTest, ATaskbarCreatedAfterTheHideIsHiddenWhenItAppears) {
  Fixture f;
  // waku started at sign-in, before Explorer created the taskbar.
  f.system.windowsPresent_ = false;
  f.system.windowsVisible_ = false;
  f.taskbar.hide();
  ASSERT_TRUE(f.store.marker.has_value());
  EXPECT_FALSE(f.store.marker->changedWindows);

  f.system.windowsPresent_ = true;
  f.system.windowsVisible_ = true;
  f.system.restartCallback();  // the recheck or TaskbarCreated
  EXPECT_FALSE(f.system.windowsVisible_);
  ASSERT_TRUE(f.store.marker.has_value());
  EXPECT_TRUE(f.store.marker->changedWindows);

  f.taskbar.restore();
  EXPECT_TRUE(f.system.windowsVisible_);
  EXPECT_FALSE(f.store.marker.has_value());
}

TEST(WindowsTaskbarTest, AStaleMarkerWithADeadPidIsUndoneBeforeAnythingElse) {
  Fixture f;
  f.system.autoHide_ = true;
  f.system.windowsVisible_ = false;  // what the crashed run left
  f.store.marker = TaskbarMarker{kOtherPid, true, true};
  EXPECT_TRUE(f.taskbar.recoverStale());
  EXPECT_EQ(f.system.calls, (QStringList{"show-windows", "autohide-off"}));
  EXPECT_FALSE(f.store.marker.has_value());
}

TEST(WindowsTaskbarTest, AMarkerOfALiveWakuIsLeftAlone) {
  Fixture f;
  f.system.alive.insert(kOtherPid);
  f.system.windowsVisible_ = false;
  f.store.marker = TaskbarMarker{kOtherPid, true, true};
  EXPECT_FALSE(f.taskbar.recoverStale());
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_TRUE(f.store.marker.has_value());
}

TEST(WindowsTaskbarTest, NoMarkerMeansNoCalls) {
  Fixture f;
  EXPECT_FALSE(f.taskbar.recoverStale());
  f.taskbar.start(false);
  EXPECT_TRUE(f.system.calls.isEmpty());
}

TEST(WindowsTaskbarTest, StartRecoversFirstThenHidesOnlyWhenAsked) {
  Fixture f;
  f.system.autoHide_ = true;
  f.system.windowsVisible_ = false;
  f.store.marker = TaskbarMarker{kOtherPid, true, true};
  f.taskbar.start(true);
  EXPECT_EQ(f.system.calls,
            (QStringList{"show-windows", "autohide-off", "autohide-on", "hide-windows"}));
}

TEST(WindowsTaskbarTest, WithoutTheFlagNothingIsHidden) {
  Fixture f;
  f.taskbar.start(false);
  EXPECT_TRUE(f.system.calls.isEmpty());
  EXPECT_EQ(f.taskbar.state(), TaskbarState::Visible);
}

TEST(WindowsTaskbarTest, TheMarkerTextRoundTripsAndRejectsOtherText) {
  const TaskbarMarker marker{4242, true, false};
  const QString text = formatTaskbarMarker(marker);
  EXPECT_TRUE(text.startsWith(QStringLiteral("taskbar hidden by waku")));
  EXPECT_TRUE(text.contains(QStringLiteral("previous_autohide=0")));
  EXPECT_EQ(parseTaskbarMarker(text), marker);
  EXPECT_FALSE(parseTaskbarMarker(QStringLiteral("hello")).has_value());
  EXPECT_FALSE(parseTaskbarMarker(QStringLiteral("taskbar hidden by waku\n")).has_value());
}

TEST(WindowsTaskbarTest, TheFileMarkerStoreWritesReadsAndDeletes) {
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

TEST(WindowsTaskbarCommandTest, HideShowAndStatusAnswerTheState) {
  FakeWindowsTaskbar taskbar;
  EXPECT_EQ(answerWindowsTaskbarCommand({"status"}, &taskbar),
            QStringLiteral("ok: taskbar: visible"));
  EXPECT_EQ(answerWindowsTaskbarCommand({"hide"}, &taskbar), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(taskbar.hides, 1);
  EXPECT_EQ(answerWindowsTaskbarCommand({"status"}, &taskbar),
            QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(answerWindowsTaskbarCommand({"show"}, &taskbar),
            QStringLiteral("ok: taskbar: visible"));
  EXPECT_EQ(taskbar.restores, 1);
}

TEST(WindowsTaskbarCommandTest, StatusNeverChangesTheTaskbar) {
  FakeWindowsTaskbar taskbar;
  answerWindowsTaskbarCommand({"status"}, &taskbar);
  EXPECT_EQ(taskbar.hides + taskbar.restores, 0);
  taskbar.state_ = TaskbarState::AutoHide;
  EXPECT_EQ(answerWindowsTaskbarCommand({"status"}, &taskbar),
            QStringLiteral("ok: taskbar: autohide-was-on"));
}

TEST(WindowsTaskbarCommandTest, BadInputIsAnError) {
  FakeWindowsTaskbar taskbar;
  EXPECT_TRUE(answerWindowsTaskbarCommand({}, &taskbar).startsWith(QStringLiteral("error: usage")));
  EXPECT_TRUE(answerWindowsTaskbarCommand({"hide", "x"}, &taskbar)
                  .startsWith(QStringLiteral("error: usage")));
  EXPECT_TRUE(answerWindowsTaskbarCommand({"explode"}, &taskbar)
                  .startsWith(QStringLiteral("error: unknown taskbar command 'explode'")));
  EXPECT_EQ(answerWindowsTaskbarCommand({"hide"}, nullptr),
            QStringLiteral("error: the taskbar is not available"));
  EXPECT_EQ(taskbar.hides, 0);
}

TEST(WindowsTaskbarCommandTest, TheControlHandlerRoutesTheVerbToTheTaskbar) {
  Harness h;
  h.start(shellConfig());
  FakeWindowsTaskbar taskbar;
  const waku::control::ControlHandler handler =
      makeControlHandler(*h.host, false, {}, {}, &taskbar);
  EXPECT_EQ(handler(QStringLiteral("taskbar hide")), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(handler(QStringLiteral("taskbar status")), QStringLiteral("ok: taskbar: hidden"));
  EXPECT_EQ(handler(QStringLiteral("taskbar show")), QStringLiteral("ok: taskbar: visible"));
  EXPECT_EQ(taskbar.hides, 1);
  EXPECT_EQ(taskbar.restores, 1);
  // Without a taskbar (offscreen platform) the verb is an error, not a crash.
  const waku::control::ControlHandler bare = makeControlHandler(*h.host, false);
  EXPECT_EQ(bare(QStringLiteral("taskbar hide")),
            QStringLiteral("error: the taskbar is not available"));
}

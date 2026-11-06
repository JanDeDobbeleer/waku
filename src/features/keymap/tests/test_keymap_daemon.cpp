#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include "features/keymap/config_watcher.h"
#include "features/keymap/keymap_daemon.h"
#include "features/keymap/testing/keymap_fakes.h"
#include "features/keymap/testing/whkdrc_sample.h"
#include "kernel/testing/hotkeys_fixtures.h"

using namespace waku::keymap;
using waku::kernel::HotkeyCombo;
using waku::kernel::ScopedFakeHotkeys;

namespace {

constexpr unsigned kAlt = 1;

// The fake hotkey table, fake launcher, windows and bar, and a log that records every line.
struct Rig {
  ScopedFakeHotkeys hotkeys;  // first: the daemon unregisters in its destructor
  FakeProcessLauncher launcher;
  FakeWindowSystem windows;
  FakeControlClient bar;
  QStringList log;
  int stops = 0;
  KeymapDaemon daemon;

  Rig() : daemon(deps()) {}

  KeymapDaemon::Deps deps() {
    KeymapDaemon::Deps deps;
    deps.launcher = &launcher;
    deps.windows = &windows;
    deps.bar = &bar;
    deps.executor = immediateExecutor();
    deps.log = [this](const QString& line) { log << line; };
    deps.stop = [this] { ++stops; };
    return deps;
  }

  bool press(unsigned modifiers, unsigned key) {
    return hotkeys.backend->press(HotkeyCombo{modifiers, key});
  }
  int logged(const QString& part) const {
    int count = 0;
    for (const QString& line : log) count += line.contains(part) ? 1 : 0;
    return count;
  }
};

}  // namespace

TEST(KeymapDaemon, TheSampleFileRegistersEveryBinding) {
  Rig rig;
  const KeymapDaemon::LoadResult result = rig.daemon.load(QString::fromUtf8(kWhkdrcSample));
  EXPECT_TRUE(result.applied);
  EXPECT_EQ(result.bindings, 53);
  EXPECT_EQ(result.conflicts, 0);
  EXPECT_EQ(rig.hotkeys.backend->registered.size(), 53u);
  EXPECT_EQ(rig.daemon.statusText(),
            QStringLiteral("bindings=53; conflicts=0; issues=0; paused=no"));
}

TEST(KeymapDaemon, APressRunsTheCommandThroughTheShell) {
  Rig rig;
  rig.daemon.load(QStringLiteral(".shell pwsh\nalt + a : komorebic focus left\n"));
  EXPECT_TRUE(rig.press(kAlt, 'A'));
  ASSERT_EQ(rig.launcher.launched.size(), 1u);
  EXPECT_EQ(rig.launcher.launched[0].program, QStringLiteral("pwsh.exe"));
  EXPECT_FALSE(rig.launcher.launched[0].args.isEmpty());
  EXPECT_TRUE(rig.bar.lines.isEmpty());
}

TEST(KeymapDaemon, CmdShellUsesCmdExe) {
  Rig rig;
  rig.daemon.load(QStringLiteral(".shell cmd\nalt + a : echo hi\n"));
  rig.press(kAlt, 'A');
  ASSERT_EQ(rig.launcher.launched.size(), 1u);
  EXPECT_EQ(rig.launcher.launched[0].program, QStringLiteral("cmd.exe"));
  EXPECT_EQ(rig.launcher.launched[0].nativeArgs, QStringLiteral("/c \"echo hi\""));
}

TEST(KeymapDaemon, ACombinationAnotherProgramOwnsIsReportedOnce) {
  Rig rig;
  rig.hotkeys.backend->taken.insert(HotkeyCombo{kAlt, 'A'});
  const QString file = QStringLiteral("alt + a : one\nalt + b : two\n");
  const KeymapDaemon::LoadResult first = rig.daemon.load(file);
  EXPECT_TRUE(first.applied);
  EXPECT_EQ(first.bindings, 1);
  EXPECT_EQ(first.conflicts, 1);
  rig.daemon.load(file);
  EXPECT_EQ(rig.daemon.conflictCount(), 1);
  EXPECT_EQ(rig.logged(QStringLiteral("taken by another program")), 1);
  EXPECT_TRUE(rig.press(kAlt, 'B'));
  EXPECT_FALSE(rig.press(kAlt, 'A'));
}

TEST(KeymapDaemon, ABadReloadKeepsThePreviousBindings) {
  Rig rig;
  rig.daemon.load(QStringLiteral("alt + a : one\nalt + b : two\n"));
  const KeymapDaemon::LoadResult bad = rig.daemon.load(QStringLiteral("nonsense\nalso nonsense\n"));
  EXPECT_FALSE(bad.applied);
  EXPECT_TRUE(bad.message.startsWith(QStringLiteral("error:")));
  EXPECT_EQ(rig.hotkeys.backend->registered.size(), 2u);
  EXPECT_TRUE(rig.press(kAlt, 'A'));
  EXPECT_EQ(rig.launcher.launched.size(), 1u);
}

TEST(KeymapDaemon, APartlyBadFileLoadsTheGoodLines) {
  Rig rig;
  const KeymapDaemon::LoadResult result =
      rig.daemon.load(QStringLiteral("alt + a : one\nalt + zzz : bad\nalt + c : three\n"));
  EXPECT_TRUE(result.applied);
  EXPECT_EQ(result.bindings, 2);
  EXPECT_EQ(result.issues, 1);
  EXPECT_EQ(rig.logged(QStringLiteral("line 2")), 1);
}

TEST(KeymapDaemon, AReloadReplacesTheBindings) {
  Rig rig;
  rig.daemon.load(QStringLiteral("alt + a : one\n"));
  rig.daemon.load(QStringLiteral("alt + b : two\n"));
  EXPECT_FALSE(rig.press(kAlt, 'A'));
  EXPECT_TRUE(rig.press(kAlt, 'B'));
  EXPECT_EQ(rig.hotkeys.backend->registered.size(), 1u);
}

TEST(KeymapDaemon, PerAppBlockUsesTheForegroundProcess) {
  Rig rig;
  rig.daemon.load(
      QStringLiteral("alt + n [\n Firefox : ff\n Default : other\n Notepad : Ignore\n]\n"));
  rig.windows.foreground = QStringLiteral("firefox.exe");
  rig.press(kAlt, 'N');
  rig.windows.foreground = QStringLiteral("notepad.exe");
  rig.press(kAlt, 'N');
  rig.windows.foreground = QStringLiteral("code.exe");
  rig.press(kAlt, 'N');
  EXPECT_EQ(rig.launcher.launched.size(), 2u);
  EXPECT_EQ(rig.logged(QStringLiteral("skipped")), 1);
}

TEST(KeymapDaemon, BlacklistedForegroundAppSilencesTheBindings) {
  Rig rig;
  rig.daemon.load(QStringLiteral(".blacklist [ \"game.exe\" ]\nalt + a : one\n"));
  rig.windows.foreground = QStringLiteral("Game.exe");
  rig.press(kAlt, 'A');
  EXPECT_TRUE(rig.launcher.launched.empty());
  rig.windows.foreground = QStringLiteral("code.exe");
  rig.press(kAlt, 'A');
  EXPECT_EQ(rig.launcher.launched.size(), 1u);
}

TEST(KeymapDaemon, PauseTogglesTheOtherBindings) {
  Rig rig;
  rig.daemon.load(QStringLiteral(".pause alt + p\n.pause_hook echo toggled\nalt + a : one\n"));
  rig.press(kAlt, 'A');
  EXPECT_EQ(rig.launcher.launched.size(), 1u);
  rig.press(kAlt, 'P');  // pause: the hook runs
  EXPECT_TRUE(rig.daemon.paused());
  EXPECT_EQ(rig.launcher.launched.size(), 2u);
  rig.press(kAlt, 'A');
  EXPECT_EQ(rig.launcher.launched.size(), 2u);  // nothing ran
  rig.press(kAlt, 'P');
  EXPECT_FALSE(rig.daemon.paused());
  rig.press(kAlt, 'A');
  EXPECT_EQ(rig.launcher.launched.size(), 4u);  // hook again on resume, then the binding
}

TEST(KeymapDaemon, BuiltInBarActionsGoThroughTheControlClient) {
  Rig rig;
  rig.daemon.load(
      QStringLiteral("alt + a : waku:shortcut launcher\n"
                     "alt + b : waku:surface hide osd\n"
                     "alt + c : waku:call volume set 30\n"
                     "alt + d : waku:reload\n"));
  rig.press(kAlt, 'A');
  rig.press(kAlt, 'B');
  rig.press(kAlt, 'C');
  rig.press(kAlt, 'D');
  EXPECT_EQ(rig.bar.lines,
            (QStringList{QStringLiteral("shortcut launcher"), QStringLiteral("surface hide osd"),
                         QStringLiteral("call volume set 30"), QStringLiteral("reload")}));
  EXPECT_EQ(rig.bar.timeouts.back(), 60000);  // a reload takes seconds
  EXPECT_TRUE(rig.launcher.launched.empty());
  EXPECT_TRUE(rig.windows.allowed.empty());
}

TEST(KeymapDaemon, ShowingASurfaceFirstAllowsTheBarToTakeTheFocus) {
  Rig rig;
  rig.daemon.load(QStringLiteral("alt + a : waku:surface show launcher\n"));
  rig.press(kAlt, 'A');
  EXPECT_EQ(rig.bar.lines,
            (QStringList{QStringLiteral("status"), QStringLiteral("surface show launcher")}));
  ASSERT_EQ(rig.windows.allowed.size(), 1u);
  EXPECT_EQ(rig.windows.allowed[0], 4242ul);
}

TEST(KeymapDaemon, ActivateOrLaunchFocusesAnExistingWindow) {
  Rig rig;
  rig.daemon.load(QStringLiteral("alt + f : waku:activate-or-launch firefox.exe start firefox\n"));
  rig.windows.hasWindow = true;
  rig.press(kAlt, 'F');
  EXPECT_EQ(rig.windows.activated, QStringList{QStringLiteral("firefox.exe")});
  EXPECT_TRUE(rig.launcher.launched.empty());
}

TEST(KeymapDaemon, ActivateOrLaunchLaunchesWhenThereIsNoWindow) {
  Rig rig;
  rig.daemon.load(QStringLiteral("alt + f : waku:activate-or-launch firefox.exe start firefox\n"));
  rig.windows.hasWindow = false;
  rig.press(kAlt, 'F');
  EXPECT_EQ(rig.windows.activated.size(), 1);
  ASSERT_EQ(rig.launcher.launched.size(), 1u);
}

TEST(KeymapDaemon, KeysStopAndReloadRunAfterTheKeyHandler) {
  Rig rig;
  rig.daemon.load(QStringLiteral("alt + s : waku:keys-stop\nalt + r : waku:keys-reload\n"));
  rig.press(kAlt, 'S');
  EXPECT_EQ(rig.stops, 0);  // deferred to the event loop
  QCoreApplication::processEvents();
  EXPECT_EQ(rig.stops, 1);
  rig.press(kAlt, 'R');  // no file loaded: the reload only logs an error
  QCoreApplication::processEvents();
  EXPECT_EQ(rig.logged(QStringLiteral("cannot read")), 1);
}

TEST(KeymapDaemon, LoadFileAndReloadIfChanged) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("keys.conf"));
  const auto write = [&path](const QByteArray& text) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
  };
  Rig rig;
  write("alt + a : one\n");
  EXPECT_TRUE(rig.daemon.loadFile(path).applied);
  EXPECT_EQ(rig.daemon.boundCount(), 1);
  const int registered = rig.hotkeys.backend->registerCalls;
  EXPECT_EQ(rig.daemon.reloadFile(true).message, QStringLiteral("ok: unchanged"));
  EXPECT_EQ(rig.hotkeys.backend->registerCalls, registered);
  write("alt + a : one\nalt + b : two\n");
  EXPECT_TRUE(rig.daemon.reloadFile(true).applied);
  EXPECT_EQ(rig.daemon.boundCount(), 2);
  write("???\n");
  EXPECT_FALSE(rig.daemon.reloadFile(true).applied);
  EXPECT_EQ(rig.daemon.boundCount(), 2);
  EXPECT_FALSE(rig.daemon.loadFile(dir.filePath(QStringLiteral("missing.conf"))).applied);
  EXPECT_EQ(rig.daemon.boundCount(), 2);
}

TEST(KeymapConfigWatcher, ACallbackFollowsAChangeOfTheFile) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("keys.conf"));
  {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("alt + a : one\n");
  }
  int calls = 0;
  QEventLoop loop;
  ConfigWatcher watcher(
      path,
      [&] {
        ++calls;
        loop.quit();
      },
      50);
  QTimer::singleShot(100, &loop, [&path] {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write("alt + b : two\n");
  });
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_GE(calls, 1);
}

#include <gtest/gtest.h>

#include "features/startup/startup_service.h"
#include "features/startup/testing/fake_startup_system.h"

using namespace waku::startup;

namespace {

const QStringList kDefaults{QStringLiteral("--register-app-bar"), QStringLiteral("--hide-taskbar")};

}  // namespace

TEST(StartupServiceArguments, DefaultsHaveNoConfigHome) {
  EXPECT_EQ(wakuArguments(StartupOptions{}), kDefaults);
}

TEST(StartupServiceArguments, OptOutsDropTheFlags) {
  StartupOptions options;
  options.registerAppBar = false;
  EXPECT_EQ(wakuArguments(options), QStringList{QStringLiteral("--hide-taskbar")});
  options.hideTaskbar = false;
  EXPECT_TRUE(wakuArguments(options).isEmpty());
}

TEST(StartupServiceArguments, ConfigHomeComesFirst) {
  StartupOptions options;
  options.configHome = QStringLiteral("D:\\cfg");
  const QStringList args = wakuArguments(options);
  ASSERT_EQ(args.size(), 4);
  EXPECT_EQ(args.at(0), QStringLiteral("--config-home"));
  EXPECT_EQ(args.at(1), QStringLiteral("D:\\cfg"));
}

TEST(StartupServiceArguments, DuplicateExtrasAreDropped) {
  StartupOptions options;
  options.extraArguments = {QStringLiteral("--hide-taskbar"), QStringLiteral("--no-tray")};
  EXPECT_EQ(wakuArguments(options),
            (QStringList{QStringLiteral("--register-app-bar"), QStringLiteral("--hide-taskbar"),
                         QStringLiteral("--no-tray")}));
}

TEST(StartupServiceCommandLine, QuotesSpacesAndQuotes) {
  EXPECT_EQ(commandLine({QStringLiteral("--a"), QStringLiteral("b")}), QStringLiteral("--a b"));
  EXPECT_EQ(commandLine({QStringLiteral("C:\\My Dir")}), QStringLiteral("\"C:\\My Dir\""));
  EXPECT_EQ(commandLine({QStringLiteral("C:\\My Dir\\")}), QStringLiteral("\"C:\\My Dir\\\\\""));
  EXPECT_EQ(commandLine({QStringLiteral("a\"b")}), QStringLiteral("\"a\\\"b\""));
  EXPECT_EQ(commandLine({QString()}), QStringLiteral("\"\""));
  EXPECT_EQ(commandLine({QStringLiteral("C:\\NoSpace\\")}), QStringLiteral("C:\\NoSpace\\"));
}

TEST(StartupServiceEnable, RegistersTheDefaults) {
  FakeStartupSystem system;
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(system.registeredArguments.size(), 1);
  EXPECT_EQ(system.registeredArguments.first(),
            QStringLiteral("--register-app-bar --hide-taskbar"));
  EXPECT_EQ(result.lines.first(),
            QStringLiteral("startup: enabled, waku.exe --register-app-bar --hide-taskbar"));
}

TEST(StartupServiceEnable, ConfigHomeWithSpacesIsQuoted) {
  FakeStartupSystem system;
  StartupOptions options;
  options.configHome = QStringLiteral("D:\\my config");
  ASSERT_TRUE(enableStartup(system, options).ok);
  EXPECT_EQ(system.registeredArguments.first(),
            QStringLiteral("--config-home \"D:\\my config\" --register-app-bar --hide-taskbar"));
}

TEST(StartupServiceEnable, MissingFromPathRegistersNothing) {
  FakeStartupSystem system;
  system.pathResult.clear();
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(system.registeredArguments.isEmpty());
  EXPECT_EQ(system.removeShortcutCalls, 0);
  ASSERT_EQ(result.lines.size(), 1);
  EXPECT_EQ(result.lines.first(),
            QStringLiteral("waku.exe is not on your PATH. Add C:\\Apps\\waku to your user PATH, "
                           "sign out and in again, then run this command again."));
}

TEST(StartupServiceEnable, OtherInstallOnPathWarnsButRegisters) {
  FakeStartupSystem system;
  system.pathResult = QStringLiteral("D:\\Other\\waku.exe");
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(system.registeredArguments.size(), 1);
  ASSERT_GE(result.lines.size(), 2);
  EXPECT_TRUE(result.lines.first().startsWith(QStringLiteral("warning:")));
  EXPECT_TRUE(result.lines.first().contains(QStringLiteral("D:\\Other\\waku.exe")));
  EXPECT_TRUE(result.lines.first().contains(QStringLiteral("C:\\Apps\\waku")));
}

TEST(StartupServiceEnable, SameFolderDifferingInCaseDoesNotWarn) {
  FakeStartupSystem system;
  system.pathResult = QStringLiteral("c:/apps/WAKU/waku.exe");
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_TRUE(result.ok);
  EXPECT_FALSE(result.lines.first().startsWith(QStringLiteral("warning:")));
}

TEST(StartupServiceEnable, RegisterErrorPropagates) {
  FakeStartupSystem system;
  system.registerError = QStringLiteral("access denied");
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.lines.last(), QStringLiteral("access denied"));
  EXPECT_EQ(system.removeShortcutCalls, 0);
}

TEST(StartupServiceEnable, ShortcutIsDeletedWithoutBackup) {
  FakeStartupSystem system;
  system.shortcutPresent = true;
  const StartupResult enabled = enableStartup(system, StartupOptions{});
  EXPECT_TRUE(enabled.ok);
  EXPECT_FALSE(system.shortcutPresent);
  EXPECT_EQ(enabled.lines.size(), 3);  // shortcut, bar, keys note
  const StartupResult disabled = disableStartup(system);
  EXPECT_TRUE(disabled.ok);
  EXPECT_EQ(disabled.lines.size(), 1);
}

TEST(StartupServiceEnable, TwiceReplacesTheTask) {
  FakeStartupSystem system;
  EXPECT_TRUE(enableStartup(system, StartupOptions{}).ok);
  StartupOptions options;
  options.hideTaskbar = false;
  EXPECT_TRUE(enableStartup(system, options).ok);
  EXPECT_EQ(system.registeredArguments.size(), 2);
  EXPECT_EQ(system.task.arguments, QStringLiteral("--register-app-bar"));
}

TEST(StartupServiceDisable, NothingRegisteredIsOk) {
  FakeStartupSystem system;
  const StartupResult result = disableStartup(system);
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(result.lines.size(), 1);
  EXPECT_EQ(result.lines.first(), QStringLiteral("startup: not enabled"));
}

TEST(StartupServiceDisable, RemovesTheTask) {
  FakeStartupSystem system;
  ASSERT_TRUE(enableStartup(system, StartupOptions{}).ok);
  const StartupResult result = disableStartup(system);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.lines.first(), QStringLiteral("startup: disabled"));
  EXPECT_FALSE(system.task.registered);
}

TEST(StartupServiceDisable, RemoveErrorPropagates) {
  FakeStartupSystem system;
  system.removeError = QStringLiteral("denied");
  const StartupResult result = disableStartup(system);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.lines.first(), QStringLiteral("denied"));
  EXPECT_EQ(system.removeShortcutCalls, 0);
}

TEST(StartupServiceStatus, NotRegistered) {
  FakeStartupSystem system;
  const StartupResult result = startupStatus(system);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.lines.mid(0, 2),
            (QStringList{QStringLiteral("startup: not registered"),
                         QStringLiteral("waku.exe: C:\\Apps\\waku\\waku.exe")}));
}

TEST(StartupServiceStatus, Enabled) {
  FakeStartupSystem system;
  system.task = {true, true, QStringLiteral("--hide-taskbar")};
  const StartupResult result = startupStatus(system);
  EXPECT_EQ(result.lines.mid(0, 3),
            (QStringList{QStringLiteral("startup: enabled"),
                         QStringLiteral("command: waku.exe --hide-taskbar"),
                         QStringLiteral("waku.exe: C:\\Apps\\waku\\waku.exe")}));
}

TEST(StartupServiceStatus, DisabledAndNotOnPath) {
  FakeStartupSystem system;
  system.pathResult.clear();
  system.task = {true, false, QString()};
  const StartupResult result = startupStatus(system);
  EXPECT_EQ(result.lines.mid(0, 3),
            (QStringList{QStringLiteral("startup: disabled"), QStringLiteral("command: waku.exe"),
                         QStringLiteral("waku.exe: not on PATH (add C:\\Apps\\waku)")}));
}

TEST(StartupServiceStatus, ReportsTheKeysDaemon) {
  FakeStartupSystem system;
  EXPECT_EQ(startupStatus(system).lines.mid(2),
            (QStringList{
                QStringLiteral("keys startup: not registered"),
                QStringLiteral("wakukeys.exe: C:\\Apps\\waku\\wakukeys.exe"),
                QStringLiteral("keys file: C:\\Users\\me\\.config\\waku\\keys.conf (missing)")}));
  system.keysConfigPresent = true;
  system.keysTask = {true, true, QStringLiteral("--config k.conf")};
  EXPECT_EQ(
      startupStatus(system).lines.mid(2),
      (QStringList{QStringLiteral("keys startup: enabled"),
                   QStringLiteral("keys command: wakukeys.exe --config k.conf"),
                   QStringLiteral("wakukeys.exe: C:\\Apps\\waku\\wakukeys.exe"),
                   QStringLiteral("keys file: C:\\Users\\me\\.config\\waku\\keys.conf (found)")}));
}

TEST(StartupServiceKeys, RegisteredWhenTheFileExists) {
  FakeStartupSystem system;
  system.keysConfigPresent = true;
  StartupOptions options;
  options.configHome = QStringLiteral("D:\\cfg");
  const StartupResult result = enableStartup(system, options);
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(system.keysRegisteredArguments.size(), 1);
  EXPECT_EQ(system.keysRegisteredArguments.first(), QStringLiteral("--config-home D:\\cfg"));
  EXPECT_TRUE(result.lines.last().startsWith(QStringLiteral("startup: keys daemon enabled")));
}

TEST(StartupServiceKeys, SkippedWithANoteWhenTheFileIsMissing) {
  FakeStartupSystem system;
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(system.keysRegisteredArguments.isEmpty());
  EXPECT_TRUE(result.lines.last().contains(QStringLiteral("no keys file")));
}

TEST(StartupServiceKeys, SkippedWithAWarningWhenNotOnPath) {
  FakeStartupSystem system;
  system.keysConfigPresent = true;
  system.keysPathResult.clear();
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(system.keysRegisteredArguments.isEmpty());
  EXPECT_TRUE(result.lines.last().startsWith(QStringLiteral("warning:")));
}

TEST(StartupServiceKeys, RegisterErrorFailsTheCommand) {
  FakeStartupSystem system;
  system.keysConfigPresent = true;
  system.keysRegisterError = QStringLiteral("denied");
  const StartupResult result = enableStartup(system, StartupOptions{});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.lines.last(), QStringLiteral("keys daemon: denied"));
  EXPECT_TRUE(system.task.registered);
}

TEST(StartupServiceKeys, DisableRemovesBothTasks) {
  FakeStartupSystem system;
  system.keysConfigPresent = true;
  ASSERT_TRUE(enableStartup(system, StartupOptions{}).ok);
  const StartupResult result = disableStartup(system);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.lines, (QStringList{QStringLiteral("startup: disabled"),
                                       QStringLiteral("startup: keys daemon disabled")}));
  EXPECT_FALSE(system.task.registered);
  EXPECT_FALSE(system.keysTask.registered);
}

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <vector>

#include "features/shell/qml/shell_service.h"

using namespace wabar::shell;

namespace {

struct Launches {
  std::vector<std::pair<QString, QStringList>> started;
  std::vector<QString> commands;
  std::vector<QString> opened;
  bool result = true;

  Shell::Launcher launcher() {
    return {[this](const QString& program, const QStringList& args) {
              started.emplace_back(program, args);
              return result;
            },
            [this](const QString& line) {
              commands.push_back(line);
              return result;
            },
            [this](const QString& target) { opened.push_back(target); }};
  }
};

}  // namespace

TEST(ShellService, StartPassesProgramAndArguments) {
  Launches l;
  Shell shell(l.launcher(), QStringLiteral("C:\\cfg"), nullptr);
  EXPECT_TRUE(shell.start(QStringLiteral("calc.exe"), {QStringLiteral("/a"), QStringLiteral("b")}));
  ASSERT_EQ(l.started.size(), 1u);
  EXPECT_EQ(l.started[0].first, QStringLiteral("calc.exe"));
  EXPECT_EQ(l.started[0].second, (QStringList{QStringLiteral("/a"), QStringLiteral("b")}));
}

TEST(ShellService, ReportsAFailedLaunch) {
  Launches l;
  l.result = false;
  Shell shell(l.launcher(), {}, nullptr);
  EXPECT_FALSE(shell.start(QStringLiteral("nope.exe")));
  EXPECT_FALSE(shell.command(QStringLiteral("echo hi")));
}

TEST(ShellService, EmptyInputIsNotLaunched) {
  Launches l;
  Shell shell(l.launcher(), {}, nullptr);
  EXPECT_FALSE(shell.start({}));
  EXPECT_FALSE(shell.command(QStringLiteral("   ")));
  shell.open({});
  shell.open(QStringLiteral(" "));
  EXPECT_TRUE(l.started.empty());
  EXPECT_TRUE(l.commands.empty());
  EXPECT_TRUE(l.opened.empty());
}

TEST(ShellService, CommandAndOpenReachTheLauncher) {
  Launches l;
  Shell shell(l.launcher(), {}, nullptr);
  EXPECT_TRUE(shell.command(QStringLiteral("echo hi && echo there")));
  shell.open(QStringLiteral("https://example.com"));
  shell.open(QStringLiteral("C:\\Users"));
  EXPECT_EQ(l.commands, (std::vector<QString>{QStringLiteral("echo hi && echo there")}));
  EXPECT_EQ(l.opened, (std::vector<QString>{QStringLiteral("https://example.com"),
                                            QStringLiteral("C:\\Users")}));
}

TEST(ShellService, ExposesPathsAndEnvironment) {
  Launches l;
  Shell shell(l.launcher(), QStringLiteral("C:\\cfg\\wabar"), nullptr);
  EXPECT_EQ(shell.configHome(), QStringLiteral("C:\\cfg\\wabar"));
  EXPECT_FALSE(shell.homeDir().isEmpty());
  EXPECT_EQ(shell.homeDir(), QDir::toNativeSeparators(QDir::homePath()));

  qputenv("WABAR_TEST_VALUE", "hello");
  EXPECT_EQ(shell.env(QStringLiteral("WABAR_TEST_VALUE")), QStringLiteral("hello"));
  EXPECT_TRUE(shell.env(QStringLiteral("WABAR_TEST_UNSET_VALUE")).isEmpty());
  qunsetenv("WABAR_TEST_VALUE");
}

TEST(ShellService, DefaultConfigHomeFollowsTheEnvironmentOrTheHomeFolder) {
  qunsetenv("WABAR_CONFIG_HOME");
  {
    Shell shell;
    EXPECT_TRUE(shell.configHome().endsWith(QStringLiteral("\\.config\\wabar")))
        << qPrintable(shell.configHome());
  }
  qputenv("WABAR_CONFIG_HOME", "D:/somewhere/else");
  {
    Shell shell;
    EXPECT_EQ(shell.configHome(), QStringLiteral("D:\\somewhere\\else"));
  }
  qunsetenv("WABAR_CONFIG_HOME");
}

// The real launches, writing into a temp folder: they start hidden detached processes.
TEST(ShellServiceReal, CommandRunsAHiddenShell) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString out = QDir::toNativeSeparators(dir.filePath(QStringLiteral("command.txt")));
  Shell shell;
  ASSERT_TRUE(shell.command(QStringLiteral("echo ran> \"%1\"").arg(out)));
  EXPECT_TRUE(QTest::qWaitFor([&] { return QFile::exists(out); }, 10000));
}

TEST(ShellServiceReal, StartRunsADetachedProgram) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // `md` needs no redirection, which Qt's argument quoting would break.
  const QString made = QDir::toNativeSeparators(dir.filePath(QStringLiteral("started")));
  Shell shell;
  ASSERT_TRUE(
      shell.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("md"), made}));
  EXPECT_TRUE(QTest::qWaitFor([&] { return QDir(made).exists(); }, 10000));
}

TEST(ShellServiceReal, StartOfAMissingProgramFails) {
  Shell shell;
  EXPECT_FALSE(shell.start(QStringLiteral("definitely-not-a-program-wabar.exe")));
}

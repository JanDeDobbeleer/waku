// `wakuc config init`: the starter shell and its writing. Temp folders only.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include "control/config_init.h"

namespace {

using waku::control::ConfigInitResult;
using waku::control::initConfigHome;

QString readAll(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool writeAll(const QString& path, const QString& text) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) >= 0;
}

}  // namespace

TEST(ConfigInit, CreatesFolderAndFile) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString home = tmp.filePath(QStringLiteral("a/b/waku"));
  const ConfigInitResult result = initConfigHome(home, QStringLiteral("ShellRoot {}\n"), false);
  ASSERT_EQ(result.status, ConfigInitResult::Written) << qPrintable(result.message);
  EXPECT_TRUE(QFileInfo(home + QStringLiteral("/shell.qml")).isFile());
  EXPECT_EQ(readAll(home + QStringLiteral("/shell.qml")), QStringLiteral("ShellRoot {}\n"));
  EXPECT_EQ(QDir(home).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 1);
}

TEST(ConfigInit, RefusesExistingShell) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString shell = tmp.filePath(QStringLiteral("shell.qml"));
  ASSERT_TRUE(writeAll(shell, QStringLiteral("mine")));
  const ConfigInitResult result = initConfigHome(tmp.path(), QStringLiteral("new"), false);
  EXPECT_EQ(result.status, ConfigInitResult::AlreadyExists);
  EXPECT_TRUE(result.message.contains(QStringLiteral("shell.qml")));
  EXPECT_EQ(readAll(shell), QStringLiteral("mine"));
}

TEST(ConfigInit, ForceOverwritesOnlyShell) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString shell = tmp.filePath(QStringLiteral("shell.qml"));
  const QString theme = tmp.filePath(QStringLiteral("theme.qml"));
  ASSERT_TRUE(writeAll(shell, QStringLiteral("mine")));
  ASSERT_TRUE(writeAll(theme, QStringLiteral("theme")));
  const ConfigInitResult result = initConfigHome(tmp.path(), QStringLiteral("new"), true);
  EXPECT_EQ(result.status, ConfigInitResult::Written);
  EXPECT_EQ(readAll(shell), QStringLiteral("new"));
  EXPECT_EQ(readAll(theme), QStringLiteral("theme"));
}

TEST(ConfigInit, FailsUnderAFile) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString blocker = tmp.filePath(QStringLiteral("blocker"));
  ASSERT_TRUE(writeAll(blocker, QStringLiteral("x")));
  const ConfigInitResult result =
      initConfigHome(blocker + QStringLiteral("/waku"), QStringLiteral("new"), false);
  EXPECT_EQ(result.status, ConfigInitResult::Failed);
  EXPECT_FALSE(result.message.isEmpty());
}

// The built-in bar's source is the template. If it stops being a shell, or the text after the
// header changes on the way, the starter breaks.
TEST(ConfigInit, StarterKeepsTheDefaultShellBody) {
  const QString source = readAll(QStringLiteral(WAKU_DEFAULT_SHELL_QML));
  ASSERT_FALSE(source.isEmpty());
  const QString starter = waku::control::starterShellText(source);
  EXPECT_TRUE(
      starter.startsWith(QStringLiteral("// The starter shell, written by `wakuc config init`")));
  EXPECT_TRUE(starter.contains(QStringLiteral("import waku\n")));
  EXPECT_TRUE(starter.contains(QStringLiteral("ShellRoot {")));
  EXPECT_FALSE(starter.contains(QStringLiteral("examples/config")));
  const qsizetype body = source.indexOf(QStringLiteral("import waku"));
  ASSERT_GE(body, 0);
  EXPECT_TRUE(starter.endsWith(source.mid(body)));
}

TEST(ConfigInit, StarterWithoutLeadingCommentIsPrefixed) {
  const QString starter = waku::control::starterShellText(QStringLiteral("import waku\n"));
  EXPECT_TRUE(starter.endsWith(QStringLiteral("\nimport waku\n")));
}

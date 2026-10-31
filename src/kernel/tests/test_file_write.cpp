#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "kernel/qt/file_write.h"

using wabar::kernel::writeFileAtomic;

namespace {

QByteArray readAll(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

TEST(FileWrite, CreatesTheFolderAndWritesTheBytes) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("a/b/state.json"));
  const auto result = writeFileAtomic(path, QByteArrayLiteral("{\"x\":1}\r\n"));
  EXPECT_TRUE(result);
  EXPECT_EQ(readAll(path), QByteArrayLiteral("{\"x\":1}\r\n"));
}

TEST(FileWrite, ReplacesAnExistingFileAndLeavesNoTemporary) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("state.json"));
  ASSERT_TRUE(writeFileAtomic(path, QByteArrayLiteral("old old old")));
  ASSERT_TRUE(writeFileAtomic(path, QByteArrayLiteral("new")));
  EXPECT_EQ(readAll(path), QByteArrayLiteral("new"));
  EXPECT_EQ(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("state.json")});
}

TEST(FileWrite, FailureKeepsTheOldFileAndSaysWhy) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // A folder where the file should be: QSaveFile cannot replace it.
  const QString path = dir.filePath(QStringLiteral("taken"));
  ASSERT_TRUE(QDir().mkpath(path));
  const auto result = writeFileAtomic(path, QByteArrayLiteral("x"));
  EXPECT_FALSE(result);
  EXPECT_NE(result.error, QFileDevice::NoError);
  EXPECT_FALSE(result.message.isEmpty());
  EXPECT_TRUE(QFileInfo(path).isDir());
}

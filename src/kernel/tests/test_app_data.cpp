#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "kernel/qt/app_data.h"

using namespace wabar::kernel;

namespace {

// Points %LOCALAPPDATA% at a temporary folder for one test; the real one is never touched.
class FakeLocalAppData {
 public:
  FakeLocalAppData() {
    EXPECT_TRUE(dir_.isValid());
    saved_ = qgetenv("LOCALAPPDATA");
    qputenv("LOCALAPPDATA", dir_.path().toUtf8());
  }
  ~FakeLocalAppData() {
    if (saved_.isNull())
      qunsetenv("LOCALAPPDATA");
    else
      qputenv("LOCALAPPDATA", saved_);
  }
  QString base() const { return dir_.path(); }
  QString yasb(const QString& name = QString()) const { return path(QStringLiteral("YASB"), name); }
  QString wabar(const QString& name = QString()) const {
    return path(QStringLiteral("wabar"), name);
  }

  static void write(const QString& file, const QByteArray& content) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(content);
  }
  static QByteArray read(const QString& file) {
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
  }

 private:
  QString path(const QString& folder, const QString& name) const {
    const QString dir = QDir(dir_.path()).filePath(folder);
    return name.isEmpty() ? dir : QDir(dir).filePath(name);
  }
  QTemporaryDir dir_;
  QByteArray saved_;
};

}  // namespace

TEST(KernelAppData, TheBaseFollowsLocalAppData) {
  FakeLocalAppData local;
  EXPECT_EQ(localAppDataBase(), local.base());
}

TEST(KernelAppData, TheFolderIsWabarUnderLocalAppDataAndCreatedOnDemand) {
  FakeLocalAppData local;
  EXPECT_FALSE(QDir(local.wabar()).exists());
  EXPECT_EQ(QDir(appDataDir()).absolutePath(), QDir(local.wabar()).absolutePath());
  EXPECT_TRUE(QDir(local.wabar()).exists());
  EXPECT_EQ(appDataDir(), appDataDir());
  EXPECT_FALSE(QDir(local.yasb()).exists()) << "the old folder is never created";
}

TEST(KernelAppDataMigration, CopiesThePinsTheTokenAndEveryTrayStateFile) {
  FakeLocalAppData local;
  local.write(local.yasb("taskbar_pinned.json"), "pins");
  local.write(local.yasb("github_token"), "token");
  local.write(local.yasb("systray_state_S1.json"), "s1");
  local.write(local.yasb("systray_state_S2.json"), "s2");
  local.write(local.yasb("unrelated.json"), "other");

  const AppDataMigration result = migrateLegacyAppData(local.base());
  EXPECT_TRUE(result.ran);
  EXPECT_EQ(result.copied.size(), 4);
  EXPECT_EQ(FakeLocalAppData::read(local.wabar("taskbar_pinned.json")), "pins");
  EXPECT_EQ(FakeLocalAppData::read(local.wabar("github_token")), "token");
  EXPECT_EQ(FakeLocalAppData::read(local.wabar("systray_state_S1.json")), "s1");
  EXPECT_EQ(FakeLocalAppData::read(local.wabar("systray_state_S2.json")), "s2");
  EXPECT_FALSE(QFile::exists(local.wabar("unrelated.json")));
}

TEST(KernelAppDataMigration, CopiesAndNeverMovesOrWritesTheOldFolder) {
  FakeLocalAppData local;
  local.write(local.yasb("github_token"), "token");
  migrateLegacyAppData(local.base());
  EXPECT_EQ(FakeLocalAppData::read(local.yasb("github_token")), "token");
  EXPECT_EQ(QDir(local.yasb()).entryList(QDir::Files).size(), 1);
}

TEST(KernelAppDataMigration, NeverOverwritesAWabarFile) {
  FakeLocalAppData local;
  local.write(local.yasb("taskbar_pinned.json"), "old");
  local.write(local.yasb("github_token"), "old-token");
  local.write(local.wabar("taskbar_pinned.json"), "mine");

  const AppDataMigration result = migrateLegacyAppData(local.base());
  EXPECT_EQ(FakeLocalAppData::read(local.wabar("taskbar_pinned.json")), "mine");
  EXPECT_EQ(FakeLocalAppData::read(local.wabar("github_token")), "old-token");
  EXPECT_EQ(result.copied, QStringList{QStringLiteral("github_token")});
  EXPECT_EQ(result.skipped, QStringList{QStringLiteral("taskbar_pinned.json")});
}

TEST(KernelAppDataMigration, DoesNothingWhenTheOldFolderIsMissing) {
  FakeLocalAppData local;
  const AppDataMigration result = migrateLegacyAppData(local.base());
  EXPECT_FALSE(result.ran);
  EXPECT_TRUE(result.copied.isEmpty());
  EXPECT_FALSE(QDir(local.wabar()).exists());
  EXPECT_FALSE(QDir(local.yasb()).exists());
}

TEST(KernelAppDataMigration, RunsOnceEvenWhenAFileIsDeletedLater) {
  FakeLocalAppData local;
  local.write(local.yasb("github_token"), "token");
  EXPECT_TRUE(migrateLegacyAppData(local.base()).ran);

  QFile::remove(local.wabar("github_token"));
  const AppDataMigration again = migrateLegacyAppData(local.base());
  EXPECT_FALSE(again.ran);
  EXPECT_TRUE(again.copied.isEmpty());
  EXPECT_FALSE(QFile::exists(local.wabar("github_token"))) << "a deleted file does not come back";
}

TEST(KernelAppDataMigration, AppDataDirMigratesBeforeItReturns) {
  FakeLocalAppData local;
  local.write(local.yasb("taskbar_pinned.json"), "pins");
  const QString dir = appDataDir();
  EXPECT_EQ(FakeLocalAppData::read(QDir(dir).filePath(QStringLiteral("taskbar_pinned.json"))),
            "pins");
}

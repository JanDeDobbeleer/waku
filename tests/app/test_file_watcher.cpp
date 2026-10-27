#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTest>

#include <memory>

#include "app/file_watcher.h"

namespace {

bool writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(contents) == contents.size();
}

class FileWatcherTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    app_ = std::make_unique<QApplication>(argc_, argv_);
  }
  static void TearDownTestSuite() { app_.reset(); }

  void SetUp() override {
    directory_ = QCoreApplication::applicationDirPath() + QStringLiteral("/u7-file-watcher-test");
    QDir(directory_).removeRecursively();
    ASSERT_TRUE(QDir().mkpath(directory_));
    config_ = directory_ + QStringLiteral("/config.yaml");
    style_ = directory_ + QStringLiteral("/styles.css");
    ASSERT_TRUE(writeFile(config_, "valid"));
    ASSERT_TRUE(writeFile(style_, "initial"));
  }
  void TearDown() override { QDir(directory_).removeRecursively(); }

  QString directory_;
  QString config_;
  QString style_;
  static inline int argc_ = 1;
  static inline char arg0_[8] = "tests";
  static inline char* argv_[2] = {arg0_, nullptr};
  static inline std::unique_ptr<QApplication> app_;
};

}  // namespace

TEST_F(FileWatcherTest, DebouncesAndWaitsForAValidConfig) {
  int configChanges = 0;
  int styleChanges = 0;
  auto valid = [this] {
    QFile file(config_);
    return file.open(QIODevice::ReadOnly) && file.readAll() == "valid";
  };
  yasb::app::FileWatcher watcher(config_, style_, valid, [&] { ++configChanges; }, [&] { ++styleChanges; }, 100);

  ASSERT_TRUE(writeFile(config_, "x"));
  QTest::qWait(250);
  EXPECT_EQ(configChanges, 0);

  ASSERT_TRUE(writeFile(config_, "broken"));
  QTest::qWait(50);
  ASSERT_TRUE(writeFile(config_, "valid"));
  QTRY_COMPARE_WITH_TIMEOUT(configChanges, 1, 3000);
  QTest::qWait(250);
  EXPECT_EQ(configChanges, 1);

  ASSERT_TRUE(writeFile(style_, "updated"));
  QTRY_COMPARE_WITH_TIMEOUT(styleChanges, 1, 3000);
}

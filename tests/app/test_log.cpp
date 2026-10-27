#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "app/log.h"

TEST(Log, RejectsDirectoryAsLogFile) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());

  EXPECT_FALSE(yasb::app::initializeLog(directory.path(), false));
}

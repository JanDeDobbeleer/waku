#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "features/workspaces/command_line.h"

TEST(CommandLine, QuoteCmdLineFollowsList2CmdLine) {
  EXPECT_EQ(waku::workspaces::quoteCmdLine({"notepad", "a b", ""}), "notepad \"a b\" \"\"");
  EXPECT_EQ(waku::workspaces::quoteCmdLine({"say \"hi\""}), "\"say \\\"hi\\\"\"");
  EXPECT_EQ(waku::workspaces::quoteCmdLine({"C:\\dir with space\\"}), "\"C:\\dir with space\\\\\"");
}

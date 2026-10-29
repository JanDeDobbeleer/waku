#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "features/workspaces/command_line.h"

TEST(CommandLine, QuoteCmdLineFollowsList2CmdLine) {
  EXPECT_EQ(wabar::workspaces::quoteCmdLine({"notepad", "a b", ""}), "notepad \"a b\" \"\"");
  EXPECT_EQ(wabar::workspaces::quoteCmdLine({"say \"hi\""}), "\"say \\\"hi\\\"\"");
  EXPECT_EQ(wabar::workspaces::quoteCmdLine({"C:\\dir with space\\"}),
            "\"C:\\dir with space\\\\\"");
}

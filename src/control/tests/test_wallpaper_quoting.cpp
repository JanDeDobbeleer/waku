// The quoting of a command line on the control pipe (`wakuc wallpaper set "C:\My
// Pictures\a.png"`).
#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "control/control.h"

TEST(WallpaperQuoting, JoinQuotesWhatNeedsIt) {
  EXPECT_EQ(waku::control::joinCommand({QStringLiteral("wallpaper"), QStringLiteral("set"),
                                        QStringLiteral("C:\\a b\\c.png")}),
            QStringLiteral("wallpaper set \"C:\\a b\\c.png\""));
  EXPECT_EQ(waku::control::joinCommand({QStringLiteral("x"), QString()}), QStringLiteral("x \"\""));
  EXPECT_EQ(waku::control::quoteArgument(QStringLiteral("say \"hi\"")),
            QStringLiteral("\"say \"\"hi\"\"\""));
  EXPECT_EQ(waku::control::quoteArgument(QStringLiteral("C:\\plain\\path.png")),
            QStringLiteral("C:\\plain\\path.png"));
}

TEST(WallpaperQuoting, SplitIsTheInverseOfJoin) {
  const QStringList words{QStringLiteral("wallpaper"),
                          QStringLiteral("set"),
                          QStringLiteral("C:\\My Pictures\\a \"best\" one.png"),
                          QString(),
                          QStringLiteral("D:\\dir with space\\"),
                          QStringLiteral("tab\there")};
  QStringList back;
  ASSERT_TRUE(waku::control::splitCommand(waku::control::joinCommand(words), &back));
  EXPECT_EQ(back, words);
}

TEST(WallpaperQuoting, SplitHandlesPlainQuotedAndBrokenLines) {
  QStringList words;
  ASSERT_TRUE(waku::control::splitCommand(QStringLiteral("  a   \"b c\"d  \"\"  "), &words));
  EXPECT_EQ(words, (QStringList{QStringLiteral("a"), QStringLiteral("b cd"), QString()}));
  ASSERT_TRUE(waku::control::splitCommand(QString(), &words));
  EXPECT_TRUE(words.isEmpty());
  EXPECT_FALSE(waku::control::splitCommand(QStringLiteral("a \"b c"), &words));
}

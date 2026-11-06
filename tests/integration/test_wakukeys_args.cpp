#include <gtest/gtest.h>

#include "app/wakukeys/keys_args.h"

namespace {

using wakukeys::KeysArgs;
using wakukeys::parseKeysArgs;

TEST(WakukeysArgs, NoArgumentsStartsTheDaemon) {
  const KeysArgs args = parseKeysArgs({});
  EXPECT_TRUE(args.error.isEmpty());
  EXPECT_TRUE(args.verb.isEmpty());
  EXPECT_TRUE(args.configFile.isEmpty());
}

TEST(WakukeysArgs, StartOptions) {
  const KeysArgs args = parseKeysArgs({QStringLiteral("--config"), QStringLiteral("a.conf"),
                                       QStringLiteral("--config-home"), QStringLiteral("home"),
                                       QStringLiteral("--log-file"), QStringLiteral("k.log")});
  EXPECT_TRUE(args.error.isEmpty());
  EXPECT_TRUE(args.verb.isEmpty());
  EXPECT_EQ(args.configFile, QStringLiteral("a.conf"));
  EXPECT_EQ(args.configHome, QStringLiteral("home"));
  EXPECT_EQ(args.logFile, QStringLiteral("k.log"));
}

TEST(WakukeysArgs, EqualsFormAndValueWithEquals) {
  const KeysArgs args = parseKeysArgs({QStringLiteral("--config=C:\\x\\a=b.conf")});
  EXPECT_TRUE(args.error.isEmpty());
  EXPECT_EQ(args.configFile, QStringLiteral("C:\\x\\a=b.conf"));
}

TEST(WakukeysArgs, Verbs) {
  for (const char* verb : {"reload", "stop", "status"}) {
    const KeysArgs args = parseKeysArgs({QString::fromLatin1(verb)});
    EXPECT_TRUE(args.error.isEmpty()) << verb;
    EXPECT_EQ(args.verb, QString::fromLatin1(verb));
  }
}

TEST(WakukeysArgs, VerbsTakeNoOptions) {
  EXPECT_FALSE(
      parseKeysArgs({QStringLiteral("--config"), QStringLiteral("a"), QStringLiteral("reload")})
          .error.isEmpty());
  EXPECT_FALSE(
      parseKeysArgs({QStringLiteral("stop"), QStringLiteral("--log-file"), QStringLiteral("a")})
          .error.isEmpty());
}

TEST(WakukeysArgs, UsageErrors) {
  EXPECT_FALSE(parseKeysArgs({QStringLiteral("start")}).error.isEmpty());
  EXPECT_FALSE(parseKeysArgs({QStringLiteral("reload"), QStringLiteral("stop")}).error.isEmpty());
  EXPECT_FALSE(parseKeysArgs({QStringLiteral("--foreground")}).error.isEmpty());
  EXPECT_FALSE(parseKeysArgs({QStringLiteral("--config")}).error.isEmpty());
  EXPECT_FALSE(parseKeysArgs({QStringLiteral("--log-file=")}).error.isEmpty());
}

}  // namespace

// Parsing of waku.exe's --font-engine option (pure; nothing starts).
#include <gtest/gtest.h>

#include "app/waku/font_engine.h"

using namespace waku::app;

TEST(FontEngineTest, TheDefaultIsGdi) {
  const FontEngineChoice choice = parseFontEngine({});
  EXPECT_EQ(choice.engine, FontEngine::Gdi);
  EXPECT_TRUE(choice.error.isEmpty());
  EXPECT_EQ(platformSpec(choice.engine), QStringLiteral("windows:fontengine=gdi"));
}

TEST(FontEngineTest, DirectWriteIsAnOptIn) {
  for (const QStringList& args :
       {QStringList{QStringLiteral("--font-engine=directwrite")},
        QStringList{QStringLiteral("--font-engine"), QStringLiteral("DirectWrite")}}) {
    const FontEngineChoice choice = parseFontEngine(args);
    EXPECT_EQ(choice.engine, FontEngine::DirectWrite);
    EXPECT_TRUE(choice.error.isEmpty());
  }
  EXPECT_EQ(platformSpec(FontEngine::DirectWrite), QStringLiteral("windows"));
}

TEST(FontEngineTest, TheLastOccurrenceWinsAndOtherArgumentsAreIgnored) {
  const FontEngineChoice choice = parseFontEngine(
      {QStringLiteral("--safe"), QStringLiteral("--font-engine=directwrite"),
       QStringLiteral("--config-home"), QStringLiteral("x"), QStringLiteral("--font-engine=gdi")});
  EXPECT_EQ(choice.engine, FontEngine::Gdi);
  EXPECT_TRUE(choice.error.isEmpty());
}

TEST(FontEngineTest, ABadValueKeepsTheDefaultAndSaysWhy) {
  const FontEngineChoice unknown = parseFontEngine({QStringLiteral("--font-engine=freetype")});
  EXPECT_EQ(unknown.engine, FontEngine::Gdi);
  EXPECT_NE(unknown.error.indexOf(QStringLiteral("freetype")), -1);
  const FontEngineChoice missing = parseFontEngine({QStringLiteral("--font-engine")});
  EXPECT_EQ(missing.engine, FontEngine::Gdi);
  EXPECT_FALSE(missing.error.isEmpty());
}
